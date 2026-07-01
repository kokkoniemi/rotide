module type Runner = sig
  val run : 'a -> int
end

module Demo = struct
  type state = Ready | Done
  type person = { name : string; age : int }

  let count = ref 0

  let run value =
    let local = value in
    Printf.printf "Value %d\n" local;
    match local with
    | 0 -> Ready
    | _ -> Done

  class worker initial = object
    val mutable current = initial
    method get = current
  end
end
