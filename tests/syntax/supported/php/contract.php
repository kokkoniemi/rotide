<!doctype html>
<main class="shell">
<?php
namespace App\Demo;

use Vendor\Package\Thing as Alias;
use function Vendor\helper as call_helper;
use const Vendor\VALUE as IMPORTED_VALUE;

final readonly class User extends Base implements Named {
    public const KIND = "user";
    private static int $count = 0;

    public function __construct(public string $name) {}

    public function greet(?string $prefix = null): string {
        $value = $prefix ?? "Hello";
        return call_helper($value) . ", {$this->name}";
    }
}

function build(array $items): User {
    foreach ($items as $item) {
        if ($item instanceof User) {
            return $item;
        }
    }
    return new User(name: "Ada");
}

$result = User::factory()->greet("Hi");
echo $result;
?>
</main>
