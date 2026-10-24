vwave.load("input.wav")
vwave.select(0.0, vwave.info().duration)
vwave.map(0.0, vwave.info().duration, function(sample, frame, channel)
    return sample * 0.5
end)
vwave.save("output.wav")
