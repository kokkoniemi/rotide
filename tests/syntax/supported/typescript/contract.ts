/** Builds a typed result. @param value source value */
import { readFile as READ_FILE } from "node:fs";

export interface User<T extends object> {
  readonly id: number;
  name?: string;
  map(value: T): Promise<T>;
}

type Result<T> = T | null;

enum Mode {
  FAST,
  Slow,
}

namespace API {
  export const VERSION = 1;
}

class Service<T extends object> implements User<T> {
  public readonly id: number = 1;
  private cache?: T;

  constructor(public name: string) {}

  async map(value: T): Promise<T> {
    this.cache = value;
    return value;
  }
}

function make<T extends object>(value: T, count?: number): Result<T> {
  const local = count ?? 1;
  return local > 0 ? value : null;
}

const arrow = (value: string): string => value;
const service = new Service<{ ok: boolean }>("demo");
console.log(make({ ok: true }), READ_FILE, Mode.FAST, API.VERSION, arrow("x"));
