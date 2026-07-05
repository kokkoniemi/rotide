-- line comment
--[[ block comment
spanning lines ]]

local M = {}

local CONST = 100

function M.new(name, age)
  return {
    name = name,
    age  = age,
    id   = CONST + 1,
  }
end

function M:greet()
  return "hi, " .. self.name
end

for i = 1, 3 do
  print(M.new("x", i).name)
end

local list = {1, 2, 3, nil, true, false}
local escape = "line\nfoo"

return M
