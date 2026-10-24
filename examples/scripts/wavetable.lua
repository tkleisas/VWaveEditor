vwave.apply("select_all", {})
vwave.wavetable("source_wavetable.vwt", 2048, 16)
vwave.synth("source_wavetable.vwt", 60, 2.0, "rendered_note.wav")
