vwave.select(0.0, 2.0)
vwave.normalize(-1.0)
vwave.fade_in(0.0, 0.02)
vwave.fade_out(1.98, 2.0)

local analysis = vwave.analyze_beats()
vwave.log(string.format("tempo: %.2f BPM", analysis.bpm))

vwave.apply("slices", { mode = "beats", bpm = analysis.bpm })
vwave.export_slices("slices", "take")
