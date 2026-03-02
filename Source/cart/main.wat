(module
  (import "env" "time_ms" (func $time_ms (result i32)))
  (import "env" "log_i32" (func $log_i32 (param i32)))

  (global $counter (mut i32) (i32.const 0))

  (func (export "cart_init") (result i32)
    call $time_ms
    call $log_i32
    i32.const 0)

  (func (export "cart_update") (result i32)
    global.get $counter
    i32.const 1
    i32.add
    global.set $counter

    global.get $counter
    i32.const 60
    i32.rem_s
    i32.const 0
    i32.eq
    if
      call $time_ms
      call $log_i32
    end

    i32.const 0)
)
