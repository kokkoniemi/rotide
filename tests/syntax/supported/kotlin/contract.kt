// line comment
/* block
   comment */

package example

import kotlin.collections.List

const val MAX_ITEMS = 100

data class User(val id: Int, val name: String, var age: Int = 18)

interface Named {
    val displayName: String
    fun greet(): String
}

class Greeter(private val greeting: String) : Named {
    override val displayName: String = "greeter"

    override fun greet(): String {
        return "$greeting, world"
    }

    companion object {
        val DEFAULT = Greeter("Hello")
    }
}

enum class Color { RED, GREEN, BLUE }

fun describe(color: Color): String = when (color) {
    Color.RED   -> "warm"
    Color.GREEN -> "cool"
    Color.BLUE  -> "cool"
}

fun main() {
    val users = listOf(User(1, "alice", 30), User(2, "bob"))
    for (u in users) {
        if (u.age >= 18) {
            println("${u.name} adult")
        } else {
            println("${u.name} minor")
        }
    }

    val g = Greeter.DEFAULT
    println(g.greet())
    println(describe(Color.RED))
}
