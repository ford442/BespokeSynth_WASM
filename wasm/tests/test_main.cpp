#include "test_common.h"

int gTestsPassed = 0;
int gTestsFailed = 0;

int main()
{
   printf("BespokeSynth WASM Test Suite\n");
   printf("============================\n");

   test_math();
   test_memory();
   test_strings();
   test_vectors();
   test_pixel_font();
   test_sdf_font();
   test_audio_buffer();
   test_audio_graph_engine();
   test_audio_process_plan();
   test_audio_graph_rt_allocations();
   test_audio_graph_benchmark();
   test_filter_module_adapter();
   test_first_wave_module_adapters();
   test_step_sequencer_note_graph();
   test_patch_state_json();
   test_patch_schema_v1_migration();
   test_typed_adapter_round_trip();
   test_reverb_adapter();
   test_sample_io_and_offline();
   test_graph_contract_pulse_voices_midi_scale();
   test_audio_health_metrics();
   test_dsp_simd_kernels();
   test_catalog_wave2_registration();
   test_catalog_wave2_mixer_ports();
   test_catalog_wave2_dynamics_and_colour();
   test_catalog_wave2_pulse_and_drums();
   test_catalog_wave2_freeverb();
   test_catalog_wave2_saved_state_fixture();
   test_scala_tuning();
   test_undo_redo();
   test_midi_learn_and_channel();
   test_script_note_queue();
   test_stereo_dsp_kernels();
   test_stereo_graph();
   test_stereo_patch_migration();

   printf("\n============================\n");
   printf("Results: %d passed, %d failed\n", gTestsPassed, gTestsFailed);
   printf("============================\n");

   return gTestsFailed > 0 ? 1 : 0;
}
