use tiny_skia::*;

fn color(c: u32) -> Color {
    Color::from_rgba8((c >> 16) as u8, (c >> 8) as u8, c as u8, (c >> 24) as u8)
}

// The caller owns a width*height BGRA DIB. No pointers are retained across calls.
#[no_mangle]
pub unsafe extern "C" fn tangos_shape(
    data: *mut u8,
    w: u32,
    h: u32,
    radius: f32,
    top: u32,
    bottom: u32,
) {
    if data.is_null() || w == 0 || h == 0 || w > 16384 || h > 16384 {
        return;
    }
    let bytes = std::slice::from_raw_parts_mut(data, w as usize * h as usize * 4);
    for p in bytes.chunks_exact_mut(4) {
        p.swap(0, 2);
        p[3] = 255;
    }
    let mut pixmap = PixmapMut::from_bytes(bytes, w, h).unwrap();
    let mut paint = Paint::default();
    paint.shader = LinearGradient::new(
        Point::from_xy(0., 0.),
        Point::from_xy(0., h as f32),
        vec![
            GradientStop::new(0., color(top)),
            GradientStop::new(1., color(bottom)),
        ],
        SpreadMode::Pad,
        Transform::identity(),
    )
    .unwrap();
    let (x, y, r) = (
        w as f32,
        h as f32,
        radius.min(w as f32 / 2.).min(h as f32 / 2.),
    );
    let mut p = PathBuilder::new();
    p.move_to(r, 0.);
    p.line_to(x - r, 0.);
    p.quad_to(x, 0., x, r);
    p.line_to(x, y - r);
    p.quad_to(x, y, x - r, y);
    p.line_to(r, y);
    p.quad_to(0., y, 0., y - r);
    p.line_to(0., r);
    p.quad_to(0., 0., r, 0.);
    p.close();
    pixmap.fill_path(
        &p.finish().unwrap(),
        &paint,
        FillRule::Winding,
        Transform::identity(),
        None,
    );
    for p in bytes.chunks_exact_mut(4) {
        p.swap(0, 2);
    }
}

#[no_mangle]
pub unsafe extern "C" fn tangos_image(data: *mut u8, w: u32, h: u32, png: *const u8, len: usize) {
    if data.is_null() || png.is_null() || w == 0 || h == 0 || w > 16384 || h > 16384 {
        return;
    }
    let mut decoder = png::Decoder::new(std::io::Cursor::new(std::slice::from_raw_parts(png, len)));
    decoder.set_transformations(png::Transformations::EXPAND | png::Transformations::STRIP_16);
    let Ok(mut reader) = decoder.read_info() else {
        return;
    };
    let mut pixels = vec![0; reader.output_buffer_size()];
    let Ok(info) = reader.next_frame(&mut pixels) else {
        return;
    };
    if info.color_type != png::ColorType::Rgba {
        return;
    }
    pixels.truncate(info.buffer_size());
    for p in pixels.chunks_exact_mut(4) {
        for c in 0..3 {
            p[c] = ((p[c] as u16 * p[3] as u16 + 127) / 255) as u8;
        }
    }
    let Some(mut image) =
        Pixmap::from_vec(pixels, IntSize::from_wh(info.width, info.height).unwrap())
    else {
        return;
    };
    // Reduce large bundled frames in stages so small mascot outlines stay antialiased.
    while image.width() > w * 2 && image.height() > h * 2 {
        let nw = (image.width() / 2).max(w);
        let nh = (image.height() / 2).max(h);
        let Some(mut scaled) = Pixmap::new(nw, nh) else {
            return;
        };
        // Average each source footprint in premultiplied RGBA. Bilinear sampling
        // alone aliases high-frequency detail when shrinking by more than 2x.
        let sw = image.width() as usize;
        let sh = image.height() as usize;
        for y in 0..nh as usize {
            let y0 = y * sh / nh as usize;
            let y1 = (y + 1) * sh / nh as usize;
            for x in 0..nw as usize {
                let x0 = x * sw / nw as usize;
                let x1 = (x + 1) * sw / nw as usize;
                let count = ((x1 - x0) * (y1 - y0)) as u32;
                let mut sum = [0u32; 4];
                for sy in y0..y1 {
                    for sx in x0..x1 {
                        for c in 0..4 {
                            sum[c] += image.data()[(sy * sw + sx) * 4 + c] as u32;
                        }
                    }
                }
                for c in 0..4 {
                    scaled.data_mut()[(y * nw as usize + x) * 4 + c] =
                        ((sum[c] + count / 2) / count) as u8;
                }
            }
        }
        image = scaled;
    }
    let bytes = std::slice::from_raw_parts_mut(data, w as usize * h as usize * 4);
    for p in bytes.chunks_exact_mut(4) {
        p.swap(0, 2);
        p[3] = 255;
    }
    let mut dst = PixmapMut::from_bytes(bytes, w, h).unwrap();
    dst.draw_pixmap(
        0,
        0,
        image.as_ref(),
        &PixmapPaint {
            quality: FilterQuality::Bilinear,
            ..PixmapPaint::default()
        },
        Transform::from_scale(
            w as f32 / image.width() as f32,
            h as f32 / image.height() as f32,
        ),
        None,
    );
    for p in bytes.chunks_exact_mut(4) {
        p.swap(0, 2);
    }
}

// Animated mesh and glass bubbles. Caller supplies a monotonic, visibility-paused phase.
#[no_mangle]
pub unsafe extern "C" fn tangos_mesh(data: *mut u8, w: u32, h: u32, seconds: f32, theme: u32) {
    if data.is_null() || w == 0 || h == 0 || w > 16384 || h > 16384 {
        return;
    }
    let palettes = [
        [0xff5cb2ec, 0xff9bd6fb, 0xff7fc400, 0xffb7e372, 0xff66bff2],
        [0xfff58a17, 0xfffcb995, 0xfff44881, 0xfff2cf49, 0xfff0564c],
        [0xff04101d, 0xff06213f, 0xff02060f, 0xff0a3357, 0xff04182f],
        [0xffff7ad5, 0xffe73c83, 0xffd6aea8, 0xffd3cfc7, 0xffcb2f41],
        [0xff00ffe1, 0xfffff700, 0xff44ff00, 0xffffea00, 0xff00ffe1],
    ];
    let palette = palettes[(theme as usize).min(4)];
    let bytes = std::slice::from_raw_parts_mut(data, w as usize * h as usize * 4);
    for p in bytes.chunks_exact_mut(4) {
        p.swap(0, 2);
        p[3] = 255;
    }
    let mut dst = PixmapMut::from_bytes(bytes, w, h).unwrap();
    dst.fill(color(palette[0]));
    let rect = Rect::from_xywh(0., 0., w as f32, h as f32).unwrap();
    let homes = [(0.76, 0.26), (0.24, 0.74), (0.58, 0.84), (0.32, 0.42)];
    for (i, (x, y)) in homes.iter().enumerate() {
        let t = seconds * 0.035 * (0.65 + i as f32 * 0.17);
        let px = ((*x + 0.2 + t).rem_euclid(1.4) - 0.2) * w as f32;
        let py = (*y + 0.05 * (t * 3. + i as f32).sin()) * h as f32;
        let center = Point::from_xy(px, py);
        let mut paint = Paint::default();
        let c = palette[i + 1];
        paint.shader = RadialGradient::new(
            center,
            0.,
            center,
            w.max(h) as f32 * 0.8,
            vec![
                GradientStop::new(0., color(c)),
                GradientStop::new(1., color(c & 0x00ffffff)),
            ],
            SpreadMode::Pad,
            Transform::identity(),
        )
        .unwrap();
        dst.fill_rect(rect, &paint, Transform::identity(), None);
    }
    for i in 0..3 {
        let phase = i as f32 * 1.7;
        let px = (0.2 + i as f32 * 0.27 + 0.04 * (seconds * 0.07 + phase).sin()) * w as f32;
        let py = (1.15 - (seconds * 0.024 + i as f32 * 0.33).rem_euclid(1.4)) * h as f32;
        let radius = w.min(h) as f32 * (0.045 + i as f32 * 0.012);
        let mut path = PathBuilder::new();
        path.push_circle(px, py, radius);
        let path = path.finish().unwrap();
        let mut paint = Paint::default();
        paint.set_color_rgba8(255, 255, 255, 22);
        dst.fill_path(
            &path,
            &paint,
            FillRule::Winding,
            Transform::identity(),
            None,
        );
        paint.set_color_rgba8(255, 255, 255, 90);
        dst.stroke_path(
            &path,
            &paint,
            &Stroke {
                width: 1.2,
                ..Stroke::default()
            },
            Transform::identity(),
            None,
        );
    }
    for p in bytes.chunks_exact_mut(4) {
        p.swap(0, 2);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn mesh_is_deterministic_and_moves() {
        let mut a = vec![0u8; 64 * 48 * 4];
        let mut b = a.clone();
        let mut c = a.clone();
        unsafe {
            tangos_mesh(a.as_mut_ptr(), 64, 48, 0., 0);
            tangos_mesh(b.as_mut_ptr(), 64, 48, 0., 0);
            tangos_mesh(c.as_mut_ptr(), 64, 48, 5., 0);
        }
        assert_eq!(a, b);
        assert_ne!(a, c);
        assert!(a.chunks_exact(4).all(|p| p[3] == 255));
    }
    #[test]
    fn image_downsampling_preserves_dense_detail_without_aliasing() {
        let mut source = vec![0u8; 129 * 129 * 4];
        for y in 0..129 {
            for x in 0..129 {
                let i = (y * 129 + x) * 4;
                source[i..i + 3].fill(if (x + y) % 2 == 0 { 255 } else { 0 });
                source[i + 3] = 255;
            }
        }
        let mut png_data = Vec::new();
        {
            let mut encoder = png::Encoder::new(&mut png_data, 129, 129);
            encoder.set_color(png::ColorType::Rgba);
            encoder.set_depth(png::BitDepth::Eight);
            encoder
                .write_header()
                .unwrap()
                .write_image_data(&source)
                .unwrap();
        }
        let mut output = vec![0u8; 9 * 9 * 4];
        unsafe {
            tangos_image(output.as_mut_ptr(), 9, 9, png_data.as_ptr(), png_data.len());
        }
        for y in 2..7 {
            for x in 2..7 {
                let sample = output[(y * 9 + x) * 4];
                assert!(
                    sample > 90 && sample < 165,
                    "aliased checkerboard sample: {sample}"
                );
            }
        }
    }
    #[test]
    fn raster_is_bgra_and_replaces_old_pixels() {
        let mut pixels = vec![0u8; 16 * 16 * 4];
        unsafe {
            tangos_shape(pixels.as_mut_ptr(), 16, 16, 0., 0xffff0000, 0xffff0000);
        }
        assert_eq!(&pixels[0..4], &[0, 0, 255, 255]);
        unsafe {
            tangos_shape(pixels.as_mut_ptr(), 16, 16, 0., 0xff0000ff, 0xff0000ff);
        }
        assert_eq!(&pixels[0..4], &[255, 0, 0, 255]);
    }
}
