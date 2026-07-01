package demo.core

import scala.collection.mutable.ListBuffer

@deprecated("legacy", "1.0")
trait Runner:
  def run[T](value: T): Int

enum State:
  case Ready, Done

class Worker(val name: String) extends Runner:
  private var count = 0

  def run[T](value: T): Int =
    val local = value
    println(s"Value $local")
    if local != null then 1 else 0

object Main:
  def main(args: Array[String]): Unit =
    val worker = new Worker("worker")
    worker.run(1)
