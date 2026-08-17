# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [2.1.2]

### Fixed

- Fix an issue in the network parser code to prevent out-of-bound writes.
  An attacker could send malicious packages to a cannelloni instance and
  cause it to crash, resulting in a DoS. CVE-2026-37539

## [2.1.1]

Maintenance release as initially 2.1.0 pointed to a valid but not the
final commit.

## [2.1.0]

### Fixed

- Fix an issue when no remote address was provided (`-R`) which lead to incorrect
  connections. Now it is `127.0.0.1` / `::1` as written in the usage (`--help`).

### Changed

- CAN frames that stay undeliverable on a stuck bus are now dropped after a
  staleness timeout instead of being buffered indefinitely and flushed once the
  bus recovers. The timeout is configurable via the new `-x <microseconds>`
  option and defaults to `2000000` (2 seconds); set `-x 0` to restore the
  previous behavior. #88
- Find and link pthreads using CMake tools
- cannelloni version is now defined using CMake
