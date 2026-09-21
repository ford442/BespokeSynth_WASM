/**
 * BespokeSynth WASM - Structured undo/redo command stack (UI thread only)
 *
 * Commands describe graph edits (spawn, delete, cable, control, move), not
 * whole-patch JSON. ModuleCanvas applies them by mutating its module map and
 * republishing an AudioGraphSnapshot, so undo never touches audio-thread state.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include "BespokeWasm/MidiMap.h"
#include <cstddef>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace bespoke
{
   namespace wasm
   {
      struct UndoConnection
      {
         int sourceModuleId = -1;
         int sourcePortIndex = 0;
         int destModuleId = -1;
         int destPortIndex = 0;
      };

      struct UndoModuleState
      {
         int id = -1;
         std::string type;
         float x = 0.0f;
         float y = 0.0f;
         bool minimized = false;
         bool enabled = true;
         std::map<std::string, float> controls;
         std::map<std::string, std::string> extras;
      };

      struct UndoCommand
      {
         enum class Kind
         {
            AddModule,
            RemoveModule,
            Connect,
            Disconnect,
            SetControl,
            MoveModule,
            Group,
         };

         Kind kind = Kind::Group;

         // AddModule / RemoveModule: the module plus every cable and CC binding
         // that referenced it, so undoing a delete restores its wiring too.
         UndoModuleState module;
         std::vector<UndoConnection> connections;
         std::vector<MidiCcBinding> bindings;

         // Connect / Disconnect
         UndoConnection connection;

         // SetControl / MoveModule
         int moduleId = -1;
         std::string control;
         float before = 0.0f;
         float after = 0.0f;
         float fromX = 0.0f;
         float fromY = 0.0f;
         float toX = 0.0f;
         float toY = 0.0f;

         // Group: applied in order on redo, reverse order on undo.
         std::vector<UndoCommand> children;

         /** Monotonic tick of the last edit (SetControl coalescing). */
         unsigned long long stamp = 0;

         static UndoCommand setControl(int moduleId, const std::string& name, float before, float after)
         {
            UndoCommand command;
            command.kind = Kind::SetControl;
            command.moduleId = moduleId;
            command.control = name;
            command.before = before;
            command.after = after;
            return command;
         }

         static UndoCommand move(int moduleId, float fromX, float fromY, float toX, float toY)
         {
            UndoCommand command;
            command.kind = Kind::MoveModule;
            command.moduleId = moduleId;
            command.fromX = fromX;
            command.fromY = fromY;
            command.toX = toX;
            command.toY = toY;
            return command;
         }

         static UndoCommand cable(Kind kind, const UndoConnection& connection)
         {
            UndoCommand command;
            command.kind = kind;
            command.connection = connection;
            return command;
         }
      };

      class UndoStack
      {
      public:
         static constexpr size_t kDefaultCapacity = 50;

         explicit UndoStack(size_t capacity = kDefaultCapacity)
         : mCapacity(capacity == 0 ? 1 : capacity)
         {
         }

         /**
          * Record a new edit and drop the redo branch. A SetControl on the same
          * module/control as the previous command within `coalesceTicks` merges
          * into it (one undo step per knob gesture or API burst).
          */
         void push(UndoCommand command, unsigned long long coalesceTicks = 0)
         {
            mRedo.clear();
            if (coalesceTicks > 0 && command.kind == UndoCommand::Kind::SetControl && !mUndo.empty())
            {
               UndoCommand& last = mUndo.back();
               if (last.kind == UndoCommand::Kind::SetControl && last.moduleId == command.moduleId &&
                   last.control == command.control && command.stamp - last.stamp <= coalesceTicks)
               {
                  last.after = command.after;
                  last.stamp = command.stamp;
                  return;
               }
            }
            mUndo.push_back(std::move(command));
            while (mUndo.size() > mCapacity)
               mUndo.pop_front();
         }

         bool canUndo() const { return !mUndo.empty(); }
         bool canRedo() const { return !mRedo.empty(); }
         size_t undoDepth() const { return mUndo.size(); }
         size_t redoDepth() const { return mRedo.size(); }
         size_t capacity() const { return mCapacity; }

         /** Pops the newest command; the caller applies its inverse then calls pushRedo. */
         UndoCommand popUndo()
         {
            UndoCommand command = std::move(mUndo.back());
            mUndo.pop_back();
            return command;
         }

         UndoCommand popRedo()
         {
            UndoCommand command = std::move(mRedo.back());
            mRedo.pop_back();
            return command;
         }

         void pushRedo(UndoCommand command) { mRedo.push_back(std::move(command)); }

         /** Re-record after a redo without clearing the remaining redo branch. */
         void pushUndoKeepRedo(UndoCommand command)
         {
            mUndo.push_back(std::move(command));
            while (mUndo.size() > mCapacity)
               mUndo.pop_front();
         }

         void clear()
         {
            mUndo.clear();
            mRedo.clear();
         }

      private:
         size_t mCapacity;
         std::deque<UndoCommand> mUndo;
         std::vector<UndoCommand> mRedo;
      };
   } // namespace wasm
} // namespace bespoke
