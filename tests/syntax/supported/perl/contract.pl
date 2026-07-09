#!/usr/bin/env perl
use strict;
use warnings;
use feature "say";

package Geometry::Point;

our $VERSION = v1.2.3;
my $origin = 0;
state $calls = 0;

sub new {
	my ($class, %args) = @_;
	return bless { x => $args{x} // 0, y => $args{y} // 0 }, $class;
}

sub distance {
	my ($self, $other) = @_;
	$calls++;
	my $dx = $self->{x} - $other->{x};
	my $dy = $self->{y} - $other->{y};
	return sqrt($dx ** 2 + $dy ** 2);
}

my @points = (
	Geometry::Point->new(x => 3, y => 4),
	Geometry::Point->new(x => 6, y => 8),
);

my %labels = (first => "alpha", second => "beta");
for my $point (@points) {
	say $labels{first} if defined $point;
}

my $quoted = q{literal};
my $words = qw(red green blue);
my $regex = qr/^[a-z]+(?:_[a-z]+)*$/i;
my $changed = "alpha_beta";
$changed =~ s/_/ /g;

if ($changed =~ $regex) {
	say "matched: $changed";
} elsif (!$origin) {
	warn "origin";
} else {
	die "unreachable";
}

__DATA__
fixture payload
