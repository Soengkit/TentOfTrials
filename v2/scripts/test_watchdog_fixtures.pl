#!/usr/bin/perl
# test_watchdog_fixtures.pl - Malformed JSON fixture coverage for log watchdog
# bounty #6: verifies valid JSON, malformed JSON, and mixed log input handling.
use strict;
use warnings;
use JSON::PP;
use File::Temp qw(tempdir);
use File::Spec;

my $tests_run = 0;
my $tests_passed = 0;
my $tests_failed = 0;

sub assert {
    my ($cond, $msg) = @_;
    $tests_run++;
    if ($cond) {
        print "PASS: $msg\n";
        $tests_passed++;
    } else {
        print "FAIL: $msg\n";
        $tests_failed++;
    }
}

sub write_file {
    my ($dir, $name, @lines) = @_;
    my $path = File::Spec->catfile($dir, $name);
    open(my $fh, '>', $path) or die "Cannot write $path: $!";
    print $fh join("\n", @lines) . "\n";
    close($fh);
    return $path;
}

sub run_watchdog {
    my ($file) = @_;
    my $script = File::Spec->catfile($ENV{PWD}, 'v2', 'scripts', 'log_watchdog.pl');
    my $out = `perl "$script" --scan --json-summary "$file" 2>/dev/null`;
    my $rc = $? >> 8;
    # Extract the JSON summary line (last line starting with {)
    my $json_line;
    for my $line (reverse split /\n/, $out) {
        if ($line =~ /^\{/) {
            $json_line = $line;
            last;
        }
    }
    my $summary;
    eval { $summary = decode_json($json_line) if $json_line; };
    return ($summary, $rc, $out);
}

my $dir = tempdir(CLEANUP => 1);

# --- Fixture 1: valid JSON log lines ---
{
    my $f = write_file($dir, 'valid.json',
        '{"level":"error","msg":"FATAL database connection failed"}',
        '{"level":"warn","msg":"timeout on request 42"}',
        '{"level":"info","msg":"request completed"}',
    );
    my ($summary, $rc, $raw) = run_watchdog($f);
    assert(defined $summary, "valid JSON: watchdog produces JSON summary");
    assert($summary->{json_lines} == 3, "valid JSON: counted 3 JSON lines");
    assert($summary->{malformed_json} == 0, "valid JSON: 0 malformed records");
    assert($rc == 0, "valid JSON: exit code 0 (no malformed)");
}

# --- Fixture 2: malformed JSON log lines ---
{
    my $f = write_file($dir, 'malformed.json',
        '{"level":"error","msg":"missing closing brace"',
        '{bad json}',
        '{"level":"warn","msg":"unterminated string}',
        '{"valid": "but truncated',
    );
    my ($summary, $rc, $raw) = run_watchdog($f);
    assert(defined $summary, "malformed JSON: watchdog does not crash");
    assert($summary->{json_lines} == 4, "malformed JSON: counted 4 JSON-like lines");
    assert($summary->{malformed_json} == 4, "malformed JSON: 4 malformed records counted");
    assert($rc == 1, "malformed JSON: exit code 1 (malformed present)");
}

# --- Fixture 3: mixed valid + malformed + plain text ---
{
    my $f = write_file($dir, 'mixed.log',
        '{"level":"error","msg":"FATAL crash"}',
        'plain text log line with FATAL error',
        '{"broken json line',
        '{"level":"info","msg":"ok"}',
        'another plain line with timeout',
        '{totally broken',
    );
    my ($summary, $rc, $raw) = run_watchdog($f);
    assert(defined $summary, "mixed: watchdog produces JSON summary");
    assert($summary->{json_lines} == 4, "mixed: counted 4 JSON-like lines");
    assert($summary->{malformed_json} == 2, "mixed: 2 malformed records counted");
    assert($rc == 1, "mixed: exit code 1 (malformed present)");
}

# --- Fixture 4: no JSON at all (plain text only) ---
{
    my $f = write_file($dir, 'plain.log',
        'FATAL error in module foo',
        'timeout waiting for response',
        'connection refused from 10.0.0.1',
    );
    my ($summary, $rc, $raw) = run_watchdog($f);
    assert(defined $summary, "plain text: watchdog produces JSON summary");
    assert($summary->{json_lines} == 0, "plain text: 0 JSON lines");
    assert($summary->{malformed_json} == 0, "plain text: 0 malformed records");
    assert($rc == 0, "plain text: exit code 0");
}

# --- Fixture 5: empty file ---
{
    my $f = write_file($dir, 'empty.log', '');
    my ($summary, $rc, $raw) = run_watchdog($f);
    assert(defined $summary, "empty file: watchdog produces JSON summary");
    assert($summary->{json_lines} == 0, "empty file: 0 JSON lines");
    assert($rc == 0, "empty file: exit code 0");
}

# --- Fixture 6: JSON summary has required fields ---
{
    my $f = write_file($dir, 'fields.json', '{"level":"error","msg":"test"}');
    my ($summary, $rc, $raw) = run_watchdog($f);
    assert(defined $summary, "fields: JSON summary produced");
    assert(exists $summary->{version}, "fields: has version");
    assert(exists $summary->{json_lines}, "fields: has json_lines");
    assert(exists $summary->{malformed_json}, "fields: has malformed_json");
    assert(exists $summary->{alerts_sent}, "fields: has alerts_sent");
    assert(exists $summary->{pattern_matches}, "fields: has pattern_matches");
}

print "\n=== Watchdog Fixture Tests ===\n";
print "Total: $tests_run  Passed: $tests_passed  Failed: $tests_failed\n";
exit($tests_failed > 0 ? 1 : 0);
