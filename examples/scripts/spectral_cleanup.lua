vwave.apply("select_all", {})
vwave.apply("spectral_gain", {
    low_hz = 200,
    high_hz = 4000,
    db = -6
})
vwave.apply("spectral_gate", { threshold_db = -48 })

vwave.select(0.0, 0.25)
vwave.fade_in()
vwave.select(1.75, 2.0)
vwave.fade_out()
