-- Test enum variant creation and matching in VM mode
-- Note: This test is designed to work with both tree-walker and VM

enum Color {
    Red,
    Green,
    Blue,
}

enum Shape {
    Circle(r),
    Rect(w, h),
    Point,
}

fn test_variant_creation() {
    let c = Color::Red
    -- Just verify it was created without error
    assert(c != null, "variant created")
}

fn test_variant_with_data() {
    let s = Shape::Circle(5)
    assert(s != null, "Circle created")
}

fn test_match_enum_tag() {
    let s = Shape::Circle(7)
    let result = match s {
        Shape::Circle(r) => "circle"
        Shape::Rect(w, h) => "rect"
        Shape::Point => "point"
        _ => "unknown"
    }
    assert(result == "circle", "match on enum Circle")
}

fn test_match_enum_extract() {
    let s = Shape::Circle(42)
    let r = match s {
        Shape::Circle(r) => r
        Shape::Rect(w, h) => w * h
        Shape::Point => 0
        _ => -1
    }
    assert(r == 42, "match enum Circle extracts value")
}

fn test_match_unit_variant() {
    let s = Shape::Point
    let r = match s {
        Shape::Circle(r) => r
        Shape::Point => 99
        _ => -1
    }
    assert(r == 99, "match on unit variant Point")
}

fn test_match_multi_data() {
    let s = Shape::Rect(3, 4)
    let r = match s {
        Shape::Rect(w, h) => w * h
        _ => 0
    }
    assert(r == 12, "match enum Rect extracts w*h")
}

fn test_match_color() {
    fn color_name(c) {
        match c {
            Color::Red => "red"
            Color::Green => "green"
            Color::Blue => "blue"
            _ => "unknown"
        }
    }
    assert(color_name(Color::Red) == "red", "match Color::Red")
    assert(color_name(Color::Green) == "green", "match Color::Green")
    assert(color_name(Color::Blue) == "blue", "match Color::Blue")
}

fn main() {
    test_variant_creation()
    test_variant_with_data()
    test_match_enum_tag()
    test_match_enum_extract()
    test_match_unit_variant()
    test_match_multi_data()
    test_match_color()
    println("vm_enum: all tests passed")
}
