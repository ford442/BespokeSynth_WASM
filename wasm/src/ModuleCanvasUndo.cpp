/**
 * BespokeSynth WASM - Canvas undo / redo (structured commands, UndoStack.h)
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#include "BespokeWasm/ModuleCanvas.h"
#include <algorithm>
#include <chrono>
#include <vector>

namespace bespoke
{
   namespace wasm
   {
      namespace
      {
         // Consecutive API/control edits on the same control within this window
         // collapse into one undo step.
         constexpr unsigned long long kUndoCoalesceMs = 400;

         /** Gathers the modules a control-only command touches; false when it changes topology. */
         bool controlOnlyModules(const UndoCommand& command, std::vector<int>& modules)
         {
            switch (command.kind)
            {
               case UndoCommand::Kind::SetControl:
                  modules.push_back(command.moduleId);
                  return true;
               case UndoCommand::Kind::MoveModule:
                  return true;
               case UndoCommand::Kind::Group:
                  return std::all_of(command.children.begin(), command.children.end(),
                                     [&modules](const UndoCommand& child)
                                     {
                                        return controlOnlyModules(child, modules);
                                     });
               default:
                  return false;
            }
         }

         unsigned long long undoClockMs()
         {
            using namespace std::chrono;
            return static_cast<unsigned long long>(
            duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
         }

      }

      UndoModuleState ModuleCanvas::captureModuleLocked(const Module& module) const
      {
         UndoModuleState state;
         state.id = module.getId();
         state.type = module.serializedType();
         state.x = module.getX();
         state.y = module.getY();
         state.minimized = module.isMinimized();
         state.enabled = module.isEnabled();
         state.controls = moduleControlMap(module);
         state.extras = moduleStringMap(module);
         return state;
      }

      void ModuleCanvas::restoreCableLocked(const UndoConnection& cable)
      {
         if (isPlaceholderLocked(cable.sourceModuleId) || isPlaceholderLocked(cable.destModuleId))
         {
            mDanglingConnections.push_back({ cable.sourceModuleId, cable.sourcePortIndex, cable.destModuleId,
                                             cable.destPortIndex });
            refreshPlaceholderBadgesLocked();
            return;
         }
         connectLocked(cable.sourceModuleId, cable.sourcePortIndex, cable.destModuleId, cable.destPortIndex);
      }

      void ModuleCanvas::restoreModuleLocked(const UndoModuleState& state)
      {
         if (createModuleOrPlaceholderLocked(state.type, state.x, state.y, state.id) < 0)
            return;
         Module* module = mModules[state.id].get();
         module->setMinimized(state.minimized);
         module->setEnabled(state.enabled);
         for (const auto& [name, value] : state.controls)
            module->setControlValue(name, value);
         for (const auto& [name, value] : state.extras)
            module->setStringProperty(name, value);
      }

      void ModuleCanvas::recordLocked(UndoCommand command, bool coalesce)
      {
         if (mUndoSuppressDepth > 0)
            return;
         command.stamp = undoClockMs();
         mUndoStack.push(std::move(command), coalesce ? kUndoCoalesceMs : 0);
      }

      void ModuleCanvas::applyCommandLocked(const UndoCommand& command, bool forward)
      {
         auto removeConnection = [this](const UndoConnection& c)
         {
            mConnections.erase(std::remove_if(mConnections.begin(), mConnections.end(),
                                              [&c](const Connection& conn)
                                              {
                                                 return conn.sourceModuleId == c.sourceModuleId &&
                                                        conn.sourcePortIndex == c.sourcePortIndex &&
                                                        conn.destModuleId == c.destModuleId &&
                                                        conn.destPortIndex == c.destPortIndex;
                                              }),
                               mConnections.end());
         };

         switch (command.kind)
         {
            case UndoCommand::Kind::AddModule:
               if (forward)
                  restoreModuleLocked(command.module);
               else
                  deleteModuleLocked(command.module.id);
               break;
            case UndoCommand::Kind::RemoveModule:
               if (forward)
               {
                  deleteModuleLocked(command.module.id);
                  break;
               }
               restoreModuleLocked(command.module);
               for (const auto& c : command.connections)
                  restoreCableLocked(c);
               for (const auto& binding : command.bindings)
               {
                  const bool present = std::any_of(mMidiCcMap.begin(), mMidiCcMap.end(),
                                                   [&binding](const MidiCcBinding& existing)
                                                   {
                                                      return existing.moduleId == binding.moduleId &&
                                                             existing.controlName == binding.controlName &&
                                                             existing.cc == binding.cc;
                                                   });
                  if (!present)
                     mMidiCcMap.push_back(binding);
               }
               break;
            case UndoCommand::Kind::Connect:
            case UndoCommand::Kind::Disconnect:
            {
               const bool add = (command.kind == UndoCommand::Kind::Connect) == forward;
               const UndoConnection& c = command.connection;
               if (add)
                  connectLocked(c.sourceModuleId, c.sourcePortIndex, c.destModuleId, c.destPortIndex);
               else
                  removeConnection(c);
               break;
            }
            case UndoCommand::Kind::SetControl:
               if (Module* module = getModule(command.moduleId))
                  module->setControlValue(command.control, forward ? command.after : command.before);
               break;
            case UndoCommand::Kind::MoveModule:
               if (Module* module = getModule(command.moduleId))
                  module->setPosition(forward ? command.toX : command.fromX, forward ? command.toY : command.fromY);
               break;
            case UndoCommand::Kind::Group:
               if (forward)
               {
                  for (const auto& child : command.children)
                     applyCommandLocked(child, true);
               }
               else
               {
                  for (auto it = command.children.rbegin(); it != command.children.rend(); ++it)
                     applyCommandLocked(*it, false);
               }
               break;
         }
      }

      void ModuleCanvas::publishUndoChangeLocked(const UndoCommand& command)
      {
         std::vector<int> modules;
         if (!controlOnlyModules(command, modules))
         {
            publishAudioGraphSnapshotLocked();
            return;
         }
         std::sort(modules.begin(), modules.end());
         modules.erase(std::unique(modules.begin(), modules.end()), modules.end());
         for (int moduleId : modules)
            syncModuleParamsLocked(moduleId);
      }

      bool ModuleCanvas::undo()
      {
         Lock lock(mMutex);
         if (!mUndoStack.canUndo())
            return false;
         cancelPointerInteraction();
         UndoSuppressor suppress(mUndoSuppressDepth);
         UndoCommand command = mUndoStack.popUndo();
         applyCommandLocked(command, false);
         publishUndoChangeLocked(command);
         mUndoStack.pushRedo(std::move(command));
         return true;
      }

      bool ModuleCanvas::redo()
      {
         Lock lock(mMutex);
         if (!mUndoStack.canRedo())
            return false;
         cancelPointerInteraction();
         UndoSuppressor suppress(mUndoSuppressDepth);
         UndoCommand command = mUndoStack.popRedo();
         applyCommandLocked(command, true);
         publishUndoChangeLocked(command);
         mUndoStack.pushUndoKeepRedo(std::move(command));
         return true;
      }

      int ModuleCanvas::undoDepth() const
      {
         Lock lock(mMutex);
         return static_cast<int>(mUndoStack.undoDepth());
      }

      int ModuleCanvas::redoDepth() const
      {
         Lock lock(mMutex);
         return static_cast<int>(mUndoStack.redoDepth());
      }

      void ModuleCanvas::clearUndoHistory()
      {
         Lock lock(mMutex);
         mUndoStack.clear();
      }

      void ModuleCanvas::commitGestureLocked()
      {
         if (mControlModuleId >= 0)
         {
            if (Module* module = getModule(mControlModuleId))
            {
               UndoCommand group;
               group.kind = UndoCommand::Kind::Group;
               for (const auto& [name, after] : moduleControlMap(*module))
               {
                  auto before = mGestureControls.find(name);
                  if (before != mGestureControls.end() && before->second != after)
                     group.children.push_back(UndoCommand::setControl(mControlModuleId, name, before->second, after));
               }
               if (group.children.size() == 1)
                  recordLocked(std::move(group.children.front()));
               else if (!group.children.empty())
                  recordLocked(std::move(group));
            }
         }
         if (mDraggedModuleId >= 0)
         {
            if (Module* module = getModule(mDraggedModuleId))
            {
               if (module->getX() != mGestureStartX || module->getY() != mGestureStartY)
                  recordLocked(UndoCommand::move(mDraggedModuleId, mGestureStartX, mGestureStartY,
                                                 module->getX(), module->getY()));
            }
         }
         mGestureControls.clear();
      }

   } // namespace wasm
} // namespace bespoke
