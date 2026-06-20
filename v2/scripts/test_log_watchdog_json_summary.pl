#!/usr/bin/perl
use strict;
use warnings;
use v5.32;

use Cwd qw(abs_path);
use File::Basename qw(dirname);
use JSON::PP;

my $script_dir = dirname(abs_path(__FILE__));
my $root = abs_path("$script_dir/../..");
my $watchdog = "$root/v2/scripts/log_watchdog.pl";

my @cases = (
    {
        name => 'valid',
        file => "$root/v2/fixtures/log_watchdog_valid.jsonl",
        exit => 0,
        valid => 2,
        malformed => 0,
        empty => 0,
    },
    {
        name => 'malformed',
        file => "$root/v2/fixtures/log_watchdog_malformed.jsonl",
        exit => 2,
        valid => 2,
        malformed => 1,
        empty => 0,
    },
    {
        name => 'mixed',
        file => "$root/v2/fixtures/log_watchdog_mixed.log",
        exit => 2,
        valid => 2,
        malformed => 2,
        empty => 1,
    },
);

for my $case (@cases) {
    my $cmd = "$^X $watchdog --json-summary $case->{file}";
    my $output = `$cmd`;
    my $exit = $? >> 8;
    die "$case->{name}: expected exit $case->{exit}, got $exit\n$output"
        if $exit != $case->{exit};

    my $summary = decode_json($output);
    for my $key (qw(valid malformed empty)) {
        my $field = $key eq 'valid' ? 'valid_records'
                  : $key eq 'malformed' ? 'malformed_records'
                  : 'empty_records';
        die "$case->{name}: expected $field=$case->{$key}, got $summary->{$field}\n$output"
            if $summary->{$field} != $case->{$key};
    }

    die "$case->{name}: leaked secret-like raw log content\n$output"
        if $output =~ /(secret-value|never-print-this)/;
}

print "log watchdog JSON summary fixture coverage passed\n";
