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
    let Some(image) = Pixmap::from_vec(pixels, IntSize::from_wh(info.width, info.height).unwrap())
    else {
        return;
    };
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
        &PixmapPaint::default(),
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

#[cfg(test)]
mod tests {
    use super::*;
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
