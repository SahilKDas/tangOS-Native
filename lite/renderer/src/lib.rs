use tiny_skia::*;

// ZIP uses the already present native DEFLATE implementation. Bounds are supplied
// by the validated central directory; no archive data is executed.
#[no_mangle]
pub unsafe extern "C" fn tangos_inflate(
    input: *const u8, input_len: usize, output: *mut u8, output_len: usize,
) -> i32 {
    if input.is_null() || output.is_null() || input_len > 128 * 1024 * 1024 ||
       output_len > 16 * 1024 * 1024 { return -1; }
    let compressed = std::slice::from_raw_parts(input, input_len);
    match miniz_oxide::inflate::decompress_to_vec_with_limit(compressed, output_len.max(1)) {
        Ok(bytes) if bytes.len() == output_len => {
            std::ptr::copy_nonoverlapping(bytes.as_ptr(), output, output_len);
            0
        }
        _ => -1,
    }
}

fn color(c: u32) -> Color {
    Color::from_rgba8((c >> 16) as u8, (c >> 8) as u8, c as u8, (c >> 24) as u8)
}

#[no_mangle]
pub unsafe extern "C" fn tangos_icon(data: *mut u8, w: u32, h: u32, icon: u32, ink: u32) {
    if data.is_null() || w == 0 || h == 0 || w > 256 || h > 256 { return; }
    let bytes = std::slice::from_raw_parts_mut(data, w as usize * h as usize * 4);
    for pixel in bytes.chunks_exact_mut(4) { pixel.swap(0, 2); pixel[3] = 255; }
    let mut pixmap = PixmapMut::from_bytes(bytes, w, h).unwrap();
    let mut path = PathBuilder::new();
    let line = |p: &mut PathBuilder, x: f32, y: f32, xx: f32, yy: f32| {
        p.move_to(x, y); p.line_to(xx, yy);
    };
    match icon {
        0 => { // Bug / report.
            path.move_to(8., 8.); path.cubic_to(8., 3., 16., 3., 16., 8.);
            path.move_to(7., 9.); path.line_to(17., 9.); path.line_to(17., 14.);
            path.cubic_to(17., 23., 7., 23., 7., 14.); path.close();
            for y in [10., 14., 18.] { line(&mut path, 3., y, 7., y); line(&mut path, 17., y, 21., y); }
            line(&mut path, 12., 9., 12., 20.);
            line(&mut path, 8., 5., 6., 3.); line(&mut path, 16., 5., 18., 3.);
        }
        1 => { // Refresh.
            path.move_to(20., 10.); path.cubic_to(19., 2., 7., 1., 4., 9.);
            path.move_to(4., 14.); path.cubic_to(5., 22., 17., 23., 20., 15.);
            path.move_to(20., 4.); path.line_to(20., 10.); path.line_to(14., 10.);
            path.move_to(4., 20.); path.line_to(4., 14.); path.line_to(10., 14.);
        }
        2 => { // Settings sliders.
            for y in [5., 12., 19.] { line(&mut path, 3., y, 21., y); }
            for (x, y) in [(8., 5.), (16., 12.), (10., 19.)] { path.push_circle(x, y, 2.5); }
        }
        3 => { // Key vault.
            path.push_circle(15.5, 7.5, 5.5);
            path.move_to(11.5, 11.5); path.line_to(3., 20.); path.line_to(3., 22.);
            path.line_to(7., 22.); path.line_to(7., 18.); path.line_to(10., 18.);
            path.line_to(10., 15.); path.line_to(12.5, 12.5);
            path.push_circle(17., 6., 0.5);
        }
        4 => line(&mut path, 6., 12., 18., 12.),
        5 => { path.move_to(6., 6.); path.line_to(18., 6.); path.line_to(18., 18.); path.line_to(6., 18.); path.close(); }
        6 => { line(&mut path, 6., 6., 18., 18.); line(&mut path, 18., 6., 6., 18.); }
        7 => { line(&mut path, 5., 19., 5., 12.); line(&mut path, 12., 19., 12., 5.); line(&mut path, 19., 19., 19., 9.); }
        9 => { path.move_to(12., 3.); path.line_to(20., 6.); path.line_to(20., 12.);
               path.cubic_to(20., 17., 16., 20., 12., 22.); path.cubic_to(8., 20., 4., 17., 4., 12.);
               path.line_to(4., 6.); path.close(); path.move_to(8., 12.); path.line_to(11., 15.); path.line_to(16., 10.); }
        10 => { path.push_circle(6., 6., 3.); path.push_circle(6., 18., 3.); path.push_circle(18., 6., 3.);
                line(&mut path, 6., 9., 6., 15.); path.move_to(18., 9.); path.cubic_to(18., 15., 12., 18., 9., 18.); }
        11 => { path.push_circle(6., 6., 3.); path.push_circle(6., 18., 3.); path.push_circle(18., 18., 3.);
                line(&mut path, 6., 9., 6., 15.); path.move_to(18., 15.); path.line_to(18., 8.);
                path.cubic_to(18., 5., 16., 4., 12., 4.); path.move_to(15., 1.); path.line_to(12., 4.); path.line_to(15., 7.); }
        12 => { path.move_to(9., 20.); path.line_to(9., 16.); path.cubic_to(3., 16., 3., 11., 5., 8.);
                path.line_to(5., 3.); path.line_to(10., 5.); path.line_to(14., 5.); path.line_to(19., 3.);
                path.line_to(19., 8.); path.cubic_to(21., 11., 21., 16., 15., 16.); path.line_to(15., 20.);
                path.move_to(9., 18.); path.cubic_to(4., 20., 5., 15., 2., 15.); }
        13 => { path.move_to(7., 3.); path.line_to(21., 12.); path.line_to(7., 21.); path.close(); }
        14 => { path.move_to(5., 5.); path.line_to(19., 5.); path.line_to(19., 19.); path.line_to(5., 19.); path.close(); }
        15 => { path.move_to(2., 3.); path.line_to(5., 3.); path.line_to(8., 16.); path.line_to(19., 16.);
                path.line_to(22., 7.); path.line_to(6., 7.); path.push_circle(9., 21., 1.); path.push_circle(18., 21., 1.); }
        16 => { // Screenshot attachment with a plus in the open upper corner.
                path.move_to(12., 3.); path.line_to(5., 3.); path.quad_to(3., 3., 3., 5.);
                path.line_to(3., 19.); path.quad_to(3., 21., 5., 21.);
                path.line_to(19., 21.); path.quad_to(21., 21., 21., 19.); path.line_to(21., 12.);
                path.push_circle(8., 8., 1.5);
                path.move_to(3., 16.); path.line_to(8., 11.); path.line_to(14., 17.);
                path.line_to(17., 14.); path.line_to(21., 18.);
                line(&mut path, 16., 5., 22., 5.); line(&mut path, 19., 2., 19., 8.); }
        17 => { // Queue generation sparkles.
                path.move_to(12., 3.); path.line_to(14., 10.); path.line_to(21., 12.);
                path.line_to(14., 14.); path.line_to(12., 21.); path.line_to(10., 14.);
                path.line_to(3., 12.); path.line_to(10., 10.); path.close();
                line(&mut path, 3., 2., 3., 6.); line(&mut path, 1., 4., 5., 4.);
                line(&mut path, 21., 18., 21., 22.); line(&mut path, 19., 20., 23., 20.); }
        _ => { path.move_to(6., 3.); path.line_to(14., 3.); path.line_to(19., 8.); path.line_to(19., 21.); path.line_to(6., 21.); path.close();
               path.move_to(14., 3.); path.line_to(14., 8.); path.line_to(19., 8.);
               for y in [12., 16.] { line(&mut path, 9., y, 16., y); } }
    }
    if let Some(path) = path.finish() {
        let mut paint = Paint::default(); paint.set_color(color(ink));
        let stroke = Stroke { width: 2., line_cap: LineCap::Round, line_join: LineJoin::Round, ..Stroke::default() };
        pixmap.stroke_path(&path, &paint, &stroke, Transform::from_scale(w as f32 / 24., h as f32 / 24.), None);
    }
    for pixel in bytes.chunks_exact_mut(4) { pixel.swap(0, 2); }
}

// The caller owns a width*height BGRA DIB. No pointers are retained across calls.
#[no_mangle]
pub unsafe extern "C" fn tangos_shape(
    data: *mut u8, w: u32, h: u32, radius: f32, top: u32, bottom: u32,
) {
    gradient_shape(data, w, h, radius, top, None, bottom, 0);
}

#[no_mangle]
pub unsafe extern "C" fn tangos_frame(data: *mut u8, w: u32, h: u32, radius: f32, fill: u32, border: u32) {
    gradient_shape(data, w, h, radius, fill, None, fill, border);
}

#[no_mangle]
pub unsafe extern "C" fn tangos_gradient_frame(data: *mut u8, w: u32, h: u32, radius: f32, top: u32, bottom: u32, border: u32) {
    gradient_shape(data, w, h, radius, top, None, bottom, border);
}

#[no_mangle]
pub unsafe extern "C" fn tangos_glass(data: *mut u8, w: u32, h: u32, gloss: u32, panel: u32, border: u32, kind: u32) {
    if kind == 1 {
        // Controller overrides .aero-panel in app.css: 15%, 4%, muted 20% edge.
        gradient_shape(data, w, h, 18., (gloss & 0x00ffffff) | 0x26000000,
            Some((gloss & 0x00ffffff) | 0x0a000000), panel, (gloss & 0x00ffffff) | 0x33000000);
        return;
    }
    if kind == 2 {
        // The elastic agent task area uses uniform 40% gloss and a 10px radius.
        let fill = (gloss & 0x00ffffff) | 0x66000000;
        gradient_shape(data, w, h, 10., fill, None, fill, border);
        return;
    }
    gradient_shape(data, w, h, 18., (gloss & 0x00ffffff) | 0x8c000000,
        Some((gloss & 0x00ffffff) | 0x0f000000), panel, border);
}

unsafe fn gradient_shape(
    data: *mut u8,
    w: u32,
    h: u32,
    radius: f32,
    top: u32,
    middle: Option<u32>,
    bottom: u32,
    border: u32,
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
    let mut stops = vec![GradientStop::new(0., color(top)), GradientStop::new(1., color(bottom))];
    if let Some(middle) = middle { stops.insert(1, GradientStop::new(0.42, color(middle))); }
    paint.shader = LinearGradient::new(
        Point::from_xy(0., 0.),
        Point::from_xy(0., h as f32),
        stops,
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
    let path = p.finish().unwrap();
    pixmap.fill_path(
        &path,
        &paint,
        FillRule::Winding,
        Transform::identity(),
        None,
    );
    if border != 0 {
        let mut paint = Paint::default(); paint.set_color(color(border));
        pixmap.stroke_path(&path, &paint, &Stroke { width: 2., ..Stroke::default() }, Transform::identity(), None);
    }
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
    fn glass_keeps_reference_translucent_middle_and_inset_border() {
        let mut pixels = vec![0u8; 100 * 100 * 4];
        unsafe { tangos_glass(pixels.as_mut_ptr(), 100, 100, 0xffeaf4fd, 0x9effffff, 0xd9ffffff, 0); }
        let channel = |x: usize, y: usize| pixels[(y * 100 + x) * 4];
        assert!(channel(50, 2) > 100);
        assert!(channel(50, 42) < 30, "CSS glass has a six-percent stop at 42 percent");
        assert!(channel(50, 97) > 140);
        assert!(channel(0, 42) > 200, "glass border must stay visible against dark backgrounds");
        assert_eq!(channel(0, 0), 0, "rounded corner must preserve its background");
    }
    #[test]
    fn controller_and_task_follow_their_reference_overrides() {
        let mut pixels = vec![0u8; 100 * 100 * 4];
        unsafe { tangos_glass(pixels.as_mut_ptr(), 100, 100, 0xffeaf4fd, 0x9effffff, 0xd9ffffff, 1); }
        let channel = |data: &[u8], x: usize, y: usize| data[(y * 100 + x) * 4];
        assert!(channel(&pixels, 50, 2) < 45);
        assert!(channel(&pixels, 50, 42) < 15);
        assert!(channel(&pixels, 0, 42) < 65);
        pixels.fill(0);
        unsafe { tangos_glass(pixels.as_mut_ptr(), 100, 100, 0xffeaf4fd, 0x9effffff, 0xd9ffffff, 2); }
        assert_eq!(channel(&pixels, 50, 20), channel(&pixels, 50, 80));
        assert!(channel(&pixels, 50, 20) >= 99 && channel(&pixels, 50, 20) <= 103);
        assert!(channel(&pixels, 0, 50) > 200);
    }
    #[test]
    fn disabled_drive_uses_reference_ten_percent_fill_and_twenty_two_percent_edge() {
        let mut pixels = vec![0u8; 100 * 44 * 4];
        unsafe { tangos_frame(pixels.as_mut_ptr(), 100, 44, 22., 0x1a0099e0, 0x380099e0); }
        let channel = |x: usize, y: usize| pixels[(y * 100 + x) * 4];
        assert!(channel(50, 22) >= 21 && channel(50, 22) <= 24);
        // CSS border-box background also sits behind the translucent border.
        assert!(channel(50, 0) >= 64 && channel(50, 0) <= 70);
        assert_eq!(channel(0, 0), 0);
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
