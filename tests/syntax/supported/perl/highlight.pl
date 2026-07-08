#!/usr/bin/env perl
# Highlighting sampler
use strict;
use warnings;
package Sample;

my $COUNT = 42;
my $pattern = qr/^item-(\d+)$/i;

sub greet {
	my ($name) = @_;
	say "Hello, $name";
	return $COUNT if $name =~ $pattern;
}

my @items = map { $_ * 2 } (1, 2, 3);
my %lookup = (answer => 42);
greet("world");
