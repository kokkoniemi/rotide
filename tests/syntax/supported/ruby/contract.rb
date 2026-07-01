# contract comment
require "json"

module Demo
  class Greeter
    @@instances = 0

    def initialize(name, prefix: "Hello", suffix: nil, *rest, **options, &block)
      @name = name
      local = prefix
      local += suffix.to_s
      puts "#{local}, #{@name}!"
      options[:loud] ? local.upcase : local
    end

    def self.build(value)
      new(value)
    end

    def label=(value)
      @label = value
    end
  end
end

numbers = [42, 3.5]
words = %w(one two)
symbols = %i(alpha beta)
pattern = /hello/i
command = `echo ok`
handler = ->(item) { item.to_s }

numbers.each do |number|
  puts number
end
