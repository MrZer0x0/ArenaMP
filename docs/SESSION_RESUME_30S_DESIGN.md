# Session recovery architecture

[Project](../README.md) · [Roadmap](../ROADMAP_RU.md)

## Scope

This is an architecture reference for robust session recovery. It is not a certification that every state transition below is implemented and tested in the public source release. Verify actual reconnect behaviour with matching compiled clients and server.

## Identity and credentials

An account identifies the player. A random installation identifier can distinguish an installation, but is not a login credential. An IP address is transport metadata and must not authenticate a resumed session.

A robust recovery credential is bound to an authenticated account, session generation and expiry. Store a token hash on the server, rotate credentials on successful recovery and prevent replay. Never put recovery credentials in ordinary logs.

## Recovery window

For an intended 30-second recovery window, transport loss may retain the logical session while suspending normal gameplay packets. A valid recovery request rebinds the new transport to that session once. Clean logout, kick, ban, deletion, shutdown or another successful login invalidate recovery.

The client shows a blocking reconnect overlay and bounded retries. Success must restore command processing, actor ownership and authoritative state together; a network-connected indicator alone is insufficient. Expiry returns to the normal login/disconnect path.

## Verification

Test loss/recovery, token replay, expired recovery, account changes, cell transitions, server restart and a second login. Ensure the old transport cannot continue writing state. Introduce protocol fields and storage changes only with explicit versioning and migrations.
