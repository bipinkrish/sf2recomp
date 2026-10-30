#!/bin/sh
# Build the BKTR merge tool with the .NET 8 SDK.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "${DOTNET:-dotnet}" build "$HERE/Sf2Bktr.csproj" -c Release
