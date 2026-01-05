-- Spec format: c"style;style;...;text"
let red_bold = c"red;bold;Error"
println(red_bold)

-- Single style
let green = c"green;Success"
println(green)

-- No style (just text)
let plain = c"Plain"
println(plain)

println("color string tests passed")
