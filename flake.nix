{
  description = "Lock-only Bluetooth presence daemon for KDE Plasma";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      forAllSystems = nixpkgs.lib.genAttrs [ "x86_64-linux" "aarch64-linux" ];
    in {
      packages = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
        in {
          default = pkgs.stdenv.mkDerivation {
            pname = "kde-bluetooth-smartlocker";
            version = "0.2.0";
            src = self;
            nativeBuildInputs = [ pkgs.cmake pkgs.ninja pkgs.qt6.wrapQtAppsHook ];
            buildInputs = [ pkgs.qt6.qtbase pkgs.qt6.qtdeclarative pkgs.kdePackages.libplasma ];
            cmakeFlags = [ "-DBUILD_TESTING=ON" ];
            doCheck = true;
            checkPhase = ''
              cd "$NIX_BUILD_TOP/$sourceRoot"
              # qmllint cannot auto-discover QML import paths (nixpkgs issue #31725),
              # so pass them explicitly with -I. The org.kde.smartlocker module is
              # provided by plasmoid/qml/org/kde/smartlocker, whose native plugin
              # libsmartlockerqml.so only exists after the build, so qmllint cannot
              # load the C++ type during lint. Hence "SmartLockerClient was not found"
              # / "Unused import" diagnostics are unavoidable and must be tolerated.
              # We still FAIL the build on genuine unresolved imports ("Failed to import").
              qmllint \
                -I "${pkgs.qt6.qtdeclarative}/lib/qt-6/qml" \
                -I "${pkgs.kdePackages.libplasma}/lib/qt-6/qml" \
                -I plasmoid/qml \
                plasmoid/contents/ui/main.qml \
                plasmoid/contents/ui/DevicePolicyRow.qml > qmllint.log 2>&1
              qmllint_status=$?
              grep -v "SmartLockerClient was not found" qmllint.log \
                | grep -v "Unused import" \
                > qmllint.filtered.log || true
              if [ "$qmllint_status" -ne 0 ] || grep -q "Failed to import" qmllint.filtered.log; then
                cat qmllint.filtered.log
                exit 1
              fi
              cd "$NIX_BUILD_TOP/$sourceRoot/build"
              ctest --output-on-failure
            '';
          };
        });

      devShells = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
        in {
          default = pkgs.mkShell {
            packages = [
              pkgs.cmake
              pkgs.gcc
              pkgs.ninja
              pkgs.qt6.qtbase
              pkgs.qt6.qtdeclarative
              pkgs.qt6.qttools
              pkgs.kdePackages.plasma-sdk
              pkgs.kdePackages.libplasma
              pkgs.kdePackages.plasma-desktop
            ];
            # NOTE: qmllint cannot auto-discover QML import paths inside `nix develop`
            # (nixpkgs issue #31725). The explicit QML_IMPORT_PATH below is the workaround.
            # Remove this hook when that issue is resolved upstream.
            shellHook = ''
              # qtdeclarative 6.11.1 (nixos-unstable) moved qmlimportscanner from
              # bin/ to libexec/; libexec is not on the mkShell PATH by default.
              export PATH="${pkgs.qt6.qtdeclarative}/libexec:$PATH"
              export QML_IMPORT_PATH="${pkgs.qt6.qtdeclarative}/lib/qt-6/qml:${pkgs.kdePackages.libplasma}/lib/qt-6/qml"
              export QML2_IMPORT_PATH="${pkgs.kdePackages.plasma-desktop}/lib/qt-6/qml:${pkgs.qt6.qtdeclarative}/lib/qt-6/qml:${pkgs.kdePackages.libplasma}/lib/qt-6/qml"
              export QT_PLUGIN_PATH="${pkgs.qt6.qtbase}/lib/qt-6/plugins"
            '';
          };
        });

      nixosModules.default = { config, lib, pkgs, ... }:
        let
          cfg = config.services.kdeBluetoothSmartlocker;
        in {
          options.services.kdeBluetoothSmartlocker = {
            enable = lib.mkEnableOption "the KDE Bluetooth SmartLocker user service";
            package = lib.mkOption {
              type = lib.types.package;
              default = self.packages.${pkgs.stdenv.hostPlatform.system}.default;
            };
            devices = lib.mkOption {
              type = lib.types.listOf lib.types.str;
              default = [];
              description = "Bluetooth device addresses (or legacy BlueZ object paths) watched by the service.";
            };
            awaySeconds = lib.mkOption {
              type = lib.types.ints.unsigned;
              default = 30;
              description = "Absence duration before the daemon requests a lock.";
            };
            snoozeSeconds = lib.mkOption {
              type = lib.types.ints.positive;
              default = 30;
              description = "Maximum daemon snooze duration.";
            };
            resumeGraceSeconds = lib.mkOption {
              type = lib.types.ints.unsigned;
              default = 30;
              description = "Grace duration after resume before locking.";
            };
            minimumPresent = lib.mkOption {
              type = lib.types.ints.positive;
              default = 1;
              description = "Number of configured devices that must be present.";
            };
            rssiThreshold = lib.mkOption {
              type = lib.types.ints.between (-100) 0;
              default = -70;
              description = "Per-device RSSI threshold in dBm.";
            };
            rssiHysteresis = lib.mkOption {
              type = lib.types.ints.positive;
              default = 5;
              description = "RSSI hysteresis in dB.";
            };
            rssiSamples = lib.mkOption {
              type = lib.types.ints.positive;
              default = 3;
              description = "RSSI averaging window size.";
            };
            prelockNotify = lib.mkOption {
              type = lib.types.bool;
              default = false;
              description = "Send an informational notification when the away countdown starts.";
            };
          };
          config = lib.mkIf cfg.enable {
            environment.systemPackages = [ cfg.package ];
            assertions = [
              {
                assertion = builtins.all (path: builtins.match ".*[[:space:]].*" path == null) cfg.devices;
                message = "kdeBluetoothSmartlocker device specs must not contain whitespace (systemd would split them): ${lib.concatMapStringsSep ", " (p: ''"${p}"'') (lib.filter (p: builtins.match ".*[[:space:]].*" p != null) cfg.devices)}";
              }
            ];
            systemd.user.services.kde-bluetooth-smartlocker = {
              description = "KDE Bluetooth SmartLocker";
              wantedBy = [ "graphical-session.target" ];
              unitConfig = {
                StartLimitIntervalSec = "10min";
                StartLimitBurst = 10;
              };
              serviceConfig.ExecStart = "${cfg.package}/bin/kde-bluetooth-smartlocker --lock-command ${pkgs.systemd}/bin/loginctl --away-seconds ${toString cfg.awaySeconds} --snooze-seconds ${toString cfg.snoozeSeconds} --resume-grace-seconds ${toString cfg.resumeGraceSeconds} --minimum-present ${toString cfg.minimumPresent} --rssi-threshold ${toString cfg.rssiThreshold} --rssi-hysteresis ${toString cfg.rssiHysteresis} --rssi-samples ${toString cfg.rssiSamples} ${lib.optionalString cfg.prelockNotify "--prelock-notify"} ${lib.concatMapStringsSep " " (path: "--device ${path}") cfg.devices}";
              serviceConfig.Restart = "on-failure";
              serviceConfig.RestartSec = "2";
              serviceConfig.NoNewPrivileges = true;
            };
          };
        };
    };
}
