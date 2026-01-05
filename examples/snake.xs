// XS Language -- Snake Game (simplified text version)
// This version demonstrates game logic without requiring terminal I/O
// Uses random module for food placement and runs a fixed simulation

import random

fn main() {
    println("=== XS Snake Game (Simulation) ===")
    println("")

    let rows = 10
    let cols = 15

    // Initialize snake in the middle
    let mid_r = rows / 2
    let mid_c = cols / 2
    var snake = [[mid_r, mid_c], [mid_r, mid_c - 1], [mid_r, mid_c - 2]]
    var food = [random.int(0, rows - 1), random.int(0, cols - 1)]
    var score = 0

    // Direction vectors
    fn step(dir, head) {
        var r = head[0]
        var c = head[1]
        match dir {
            "up"    => { r = r - 1 },
            "down"  => { r = r + 1 },
            "left"  => { c = c - 1 },
            "right" => { c = c + 1 },
            _ => {}
        }
        return [r, c]
    }

    fn opposite(d) {
        match d {
            "up"    => { return "down" },
            "down"  => { return "up" },
            "left"  => { return "right" },
            "right" => { return "left" },
            _       => { return "" }
        }
    }

    fn draw_board(rows, cols, snake, food) {
        var border = "+"
        for j in 0..cols {
            border = border ++ "--"
        }
        border = border ++ "+"
        println(border)
        for i in 0..rows {
            var row = "|"
            for j in 0..cols {
                var is_snake = false
                var is_head = false
                if snake[0][0] == i {
                    if snake[0][1] == j {
                        is_head = true
                    }
                }
                if !is_head {
                    for s in snake {
                        if s[0] == i {
                            if s[1] == j {
                                is_snake = true
                            }
                        }
                    }
                }
                if is_head {
                    row = row ++ "> "
                } else if is_snake {
                    row = row ++ "# "
                } else if food[0] == i {
                    if food[1] == j {
                        row = row ++ "o "
                    } else {
                        row = row ++ ". "
                    }
                } else {
                    row = row ++ ". "
                }
            }
            row = row ++ "|"
            println(row)
        }
        println(border)
    }

    // Simulate a few moves
    let moves = ["right", "right", "right", "down", "down", "left", "left", "up"]
    var dir = "right"

    println("Initial board:")
    draw_board(rows, cols, snake, food)
    println("Score: {score}")
    println("")

    for move in moves {
        if move != opposite(dir) {
            dir = move
        }

        var head = step(dir, snake[0])

        // Wrap around
        if head[0] < 0 { head[0] = rows - 1 }
        if head[0] >= rows { head[0] = 0 }
        if head[1] < 0 { head[1] = cols - 1 }
        if head[1] >= cols { head[1] = 0 }

        // Check self collision
        var collision = false
        for s in snake {
            if s[0] == head[0] {
                if s[1] == head[1] {
                    collision = true
                }
            }
        }

        if collision {
            println("Game Over! Collision at [{head[0]}, {head[1]}]")
            break
        }

        // Insert new head
        var new_snake = [head]
        for s in snake {
            new_snake.push(s)
        }
        snake = new_snake

        // Check food
        if head[0] == food[0] {
            if head[1] == food[1] {
                score = score + 1
                food = [random.int(0, rows - 1), random.int(0, cols - 1)]
                println("Ate food! Score: {score}")
            } else {
                snake.pop()
            }
        } else {
            snake.pop()
        }

        println("Move: {move}  Head: [{head[0]}, {head[1]}]  Snake len: {len(snake)}")
    }

    println("")
    println("Final board:")
    draw_board(rows, cols, snake, food)
    println("Final Score: {score}")
}
