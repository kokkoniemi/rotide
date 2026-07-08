use strict;
my $value = 41;
sub increment {
	my ($number) = @_;
	return $number + 1;
}
print increment($value);
