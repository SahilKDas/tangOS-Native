// Native translation of GradientBackground.tsx's currently selected scrolling modes.
// Seeded homes make captures reproducible; the source uses random homes on mount.
#[derive(Clone, Copy, Debug)]
pub struct Mover {
    pub bx: f64,
    pub by: f64,
    pub ax: f64,
    pub ay: f64,
    pub fx: f64,
    pub fy: f64,
    pub ph: f64,
    pub spd: f64,
    pub size: f64,
    pub rot: f64,
}
struct Random(u32);
impl Random {
    fn range(&mut self, a: f64, b: f64) -> f64 {
        self.0 = self.0.wrapping_mul(1664525).wrapping_add(1013904223);
        a + f64::from(self.0) / 4294967296. * (b - a)
    }
    fn mover(&mut self) -> Mover {
        let mut m = Mover {
            bx: self.range(12., 88.),
            by: self.range(15., 85.),
            ax: self.range(6., 16.),
            ay: self.range(6., 16.),
            fx: self.range(0.5, 1.3),
            fy: self.range(0.5, 1.3),
            ph: self.range(0., std::f64::consts::TAU),
            spd: self.range(0.6, 1.4),
            size: 0.,
            rot: 0.,
        };
        // Keep the random sequence identical to the source's unused R/vx/vy.
        self.range(18., 40.);
        self.range(-1., 1.);
        self.range(-1., 1.);
        m.size = 0.;
        m
    }
}
fn space(list: &mut [Mover], minimum: f64) {
    for _ in 0..14 {
        for i in 0..list.len() {
            for j in i + 1..list.len() {
                let dx = list[j].bx - list[i].bx;
                let dy = list[j].by - list[i].by;
                let d = dx.hypot(dy);
                if d > 0.001 && d < minimum {
                    let p = (minimum - d) / 2.;
                    list[i].bx -= dx / d * p;
                    list[i].by -= dy / d * p;
                    list[j].bx += dx / d * p;
                    list[j].by += dy / d * p;
                }
            }
        }
    }
    for m in list {
        m.bx = m.bx.clamp(13., 87.);
        m.by = m.by.clamp(13., 87.);
    }
}
pub fn movers(theme: u32) -> (Vec<Mover>, Vec<Mover>) {
    let homes = [
        [(76., 26.), (24., 74.), (58., 84.), (32., 42.)],
        [(71.5, 38.7), (67.9, 70.5), (32.5, 53.5), (38.5, 84.9)],
        [(60.9, 34.1), (37.1, 76.1), (68.8, 72.4), (87., 48.5)],
        [(71.5, 38.7), (67.9, 70.5), (32.5, 53.5), (38.5, 84.9)],
        [(87., 52.8), (13.5, 47.), (29.1, 80.3), (0., 0.)],
    ][theme.min(4) as usize];
    let mut rng = Random(0x54a69000 + theme.min(4));
    let mut stops = Vec::new();
    for &(x, y) in homes.iter().take(if theme == 4 { 3 } else { 4 }) {
        let mut m = rng.mover();
        m.bx = x;
        m.by = y;
        stops.push(m);
    }
    space(&mut stops, 32.);
    let mut bubbles = Vec::new();
    for _ in 0..3 {
        let size = rng.range(90., 190.);
        let mut m = rng.mover();
        m.size = size;
        m.rot = rng.range(-8., 8.);
        bubbles.push(m);
    }
    space(&mut bubbles, 24.);
    (stops, bubbles)
}
pub fn position(m: Mover, seconds: f64, speed: f64, rising: bool) -> (f64, f64, f64) {
    let t = seconds.max(0.) * speed;
    if seconds <= 0. {
        return (m.bx, m.by, m.rot * (m.ph).sin());
    }
    let wrap = |v: f64| (v + 20.).rem_euclid(140.) - 20.;
    let x = if rising {
        m.bx + m.ax * 0.35 * (t * m.fx + m.ph).sin()
    } else {
        wrap(m.bx + 10. * t * m.spd)
    };
    let y = if rising {
        wrap(m.by - 10. * t * m.spd)
    } else {
        m.by + m.ay * 0.35 * (t * m.fy + m.ph).sin()
    };
    (x, y, m.rot * (t * m.fx * 0.5 + m.ph).sin())
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn original_motion_fixtures() {
        for line in include_str!("../tests/background-reference.csv").lines() {
            if line.is_empty() {
                continue;
            }
            let n: Vec<f64> = line.split(',').map(|v| v.parse().unwrap()).collect();
            let (stops, bubbles) = movers(n[0] as u32);
            let bubble = n[1] != 0.;
            let m = if bubble {
                bubbles[n[2] as usize]
            } else {
                stops[n[2] as usize]
            };
            let p = position(
                m,
                n[3],
                if bubble {
                    0.41
                } else if n[0] == 4. {
                    0.22
                } else {
                    0.35
                },
                bubble,
            );
            assert!(
                (p.0 - n[4]).abs() < 1e-8 && (p.1 - n[5]).abs() < 1e-8 && (p.2 - n[6]).abs() < 1e-8,
                "source motion disagreement: {line}; {p:?}"
            );
            assert!((m.size - n[7]).abs() < 1e-8);
        }
    }
}
// Per-blob filter blur is the convolution of the CSS radial alpha with a 2-D
// Gaussian. Cache radial samples rather than allocating viewport-sized layers.
pub fn blurred_profile(radius: f32, sigma: f32) -> Vec<(f32, f32)> {
    thread_local! {
        static CACHE:std::cell::RefCell<Vec<(u32,u32,Vec<(f32,f32)>)>> =
            const {std::cell::RefCell::new(Vec::new())};
    }
    CACHE.with(|cache| {
        let mut cache = cache.borrow_mut();
        if let Some((_, _, profile)) = cache
            .iter()
            .find(|(r, s, _)| *r == radius.to_bits() && *s == sigma.to_bits())
        {
            return profile.clone();
        }
        let extent = radius + 3. * sigma;
        let mut weights = Vec::new();
        for i in -6..=6 {
            let v = i as f32 * 0.5;
            weights.push((v, (-0.5 * v * v).exp()));
        }
        let norm: f32 = weights.iter().map(|(_, w)| w).sum();
        let mut profile = Vec::new();
        for i in 0..=96 {
            let t = i as f32 / 96.;
            let r = t * extent;
            let mut alpha = 0.;
            for &(x, wx) in &weights {
                for &(y, wy) in &weights {
                    let distance = (r + sigma * x).hypot(sigma * y);
                    alpha += (1. - distance / radius).max(0.) * wx * wy / (norm * norm);
                }
            }
            profile.push((t, alpha.clamp(0., 1.)));
        }
        if cache.len() >= 8 {
            cache.remove(0);
        }
        cache.push((radius.to_bits(), sigma.to_bits(), profile.clone()));
        profile
    })
}
