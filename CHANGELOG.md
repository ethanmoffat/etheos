# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.7.1] - 2026-03-24

### Added
- Scripts that install build dependencies on Windows and Linux (Ubuntu and Alpine), and build scripts for both.
- Unit tests (GoogleTest) and integration tests that run EOBot scripts against a Docker image of the server.
- SQL Server database support on Windows and Linux (via ODBC), including managed identity authentication (`DBAuthType`).
- bcrypt password hashing. Passwords are versioned (`PasswordCurrentVersion`, `BcryptWorkload`), and a player's password is upgraded to the current version in the background when they log in.
- Account creation, login and password changes run on a background thread pool (`ThreadPoolThreads`), so slow database or hashing work doesn't stall the server. Concurrent logins are limited by `LoginQueueSize`.
- Automatic database creation on first start (`AutoCreateDatabase`, `InstallSQL`), and reading the database password from a file (`DBPassFile`).
- NPC chatter, loaded from `data/speech.ini` (`SpeechFile`).
- Weddings: marriage approval and divorce at the law office, and the ceremony with the priest, with configurable prices, minimum level, rings, outfits and music.
- Sleeping at inns (`InnSleepCostBase`, `InnSleepCostPerHP`, `InnSleepCostPerTP`).
- Race-based home spawn points in `data/home.ini`.
- Timed map quakes.
- Packet logging to the console (`IgnorePacketFamilies`).
- The world state is dumped to a file when the server crashes and restored on the next start (`WorldDumpFile`).
- Any configuration value can be overridden with an `ETHEOS_<KEY>` environment variable.
- A Docker image, published to Docker Hub as `darthchungis/etheos`.
- Admin commands: `$shutdown` enhancements, `$reload` and `$cancel` for restart management, `$teach` and `$forget` for spells
- Admin command `$audit` and command audit tracking
- Skill point changes from quests and the `$setskillpoints` command are sent to the client.
- Level up emotes are shown to nearby players.
- The official packets for using an item from outside the inventory, a full locker, invalid jukebox requests and changing an accepted trade offer.
- Connection spam mitigations (`HangupDelay`, `QuietConnectionErrors`) and control over connection logging (`LogConnection`).
- Random delays for chest item spawns (`ChestSpawnRandomization`).
- A custom client download URL for SLN (`SLNClient`).
- `InitLoginBan`, which chooses how a banned account's login attempt is answered.
- UTF-8 console output on Windows.

### Changed
- EOSERV is renamed to ETHEOS, and the executable is now `etheos`.
- Windows builds use Visual Studio (MinGW is no longer supported).
- New databases use bcrypt password hashing by default.
- Pre-game packets (connection, account, character and welcome) are validated against the official packet formats.
- Version 27 client compatibility (`OldVersionCompat`) is removed.

ETHEOS is a fork of [EOSERV](https://eoserv.net), starting from EOSERV revision 535 (shortly after the EOSERV 0.7.0 release). This changelog starts at 0.7.1, the first ETHEOS version, and only lists changes made since the fork.

[Unreleased]: https://github.com/ethanmoffat/etheos/compare/0.7.1...HEAD
[0.7.1]: https://github.com/ethanmoffat/etheos/compare/046b984...0.7.1
