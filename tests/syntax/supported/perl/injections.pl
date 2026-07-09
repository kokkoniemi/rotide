use strict;
my $value = "name";
$value =~ s/name/sprintf($value)/e;

my $page = <<HTML;
<section class="card">Hello</section>
HTML
print $page;
