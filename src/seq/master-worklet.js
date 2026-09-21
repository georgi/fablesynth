// Input-side OTT for SQ-4's master bus. Kept in an AudioWorklet so its
// multiband detector remains sample-accurate with every device feeding it.
class MasterOtt extends AudioWorkletProcessor {
  constructor() {
    super();
    this.p = { on: 0, depth: .35, time: 1, up: 1, down: 1 };
    this.low = [0, 0]; this.high = [0, 0]; this.env = [0, 0, 0];
    this.gain = [1, 1, 1]; this.target = [1, 1, 1]; this.wet = 0; this.tick = 0;
    this.port.onmessage = e => { if (e.data?.t === 'params') this.p = { ...this.p, ...e.data.params }; };
  }
  process(inputs, outputs) {
    const input = inputs[0], output = outputs[0];
    const l = input[0] ?? [], r = input[1] ?? l, ol = output[0], or = output[1] ?? ol;
    const sr = sampleRate, p = this.p;
    const lowC = 1 - Math.exp(-2 * Math.PI * 120 / sr), highC = 1 - Math.exp(-2 * Math.PI * 2500 / sr);
    const smooth = 1 - Math.exp(-1 / (.015 * sr)), targetWet = p.on > .5 ? Math.max(0, Math.min(1, p.depth)) : 0;
    const time = Math.max(.01, Math.min(10, p.time));
    for (let i = 0; i < ol.length; i++) {
      const inL = l[i] || 0, inR = r[i] || 0;
      if (targetWet === 0 && this.wet < 1e-6) { this.wet = 0; ol[i] = inL; or[i] = inR; continue; }
      const loL = this.low[0] += lowC * (inL - this.low[0]);
      const loR = this.low[1] += lowC * (inR - this.low[1]);
      const hiL = this.high[0] += highC * (inL - loL - this.high[0]);
      const hiR = this.high[1] += highC * (inR - loR - this.high[1]);
      const bands = [[loL, loR], [hiL, hiR], [inL - loL - hiL, inR - loR - hiR]];
      let wetL = 0, wetR = 0;
      for (let b = 0; b < 3; b++) {
        const pk = Math.max(Math.abs(bands[b][0]), Math.abs(bands[b][1]));
        const attack = 1 - Math.exp(-1 / ((b === 0 ? .008 : b === 1 ? .003 : .001) * time * sr));
        const release = 1 - Math.exp(-1 / ((b === 0 ? .18 : b === 1 ? .12 : .08) * time * sr));
        this.env[b] += (pk - this.env[b]) * (pk > this.env[b] ? attack : release);
        if (this.tick === 0) {
          const db = 20 * Math.log10(Math.max(1e-9, this.env[b]));
          const floor = Math.max(0, Math.min(1, (db + 90) / 18));
          const boost = Math.min(24, Math.max(0, -36 - db) * .75) * Math.max(0, p.up) * floor;
          const cut = Math.max(0, db + 18) * .9 * Math.max(0, p.down);
          this.target[b] = Math.pow(10, (boost - cut) / 20);
        }
        this.gain[b] += (this.target[b] - this.gain[b]) * smooth;
        wetL += bands[b][0] * this.gain[b]; wetR += bands[b][1] * this.gain[b];
      }
      this.tick = (this.tick + 1) & 15;
      this.wet += (targetWet - this.wet) * smooth;
      ol[i] = inL + this.wet * (wetL - inL); or[i] = inR + this.wet * (wetR - inR);
    }
    return true;
  }
}
registerProcessor('fable-sq-master-ott', MasterOtt);
