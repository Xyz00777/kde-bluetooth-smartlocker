{
  description = "Lock-only Bluetooth presence daemon for desktop environments";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  inputs.nixpkgs-qt612.url = "github:NixOS/nixpkgs/4fe25e12eb9f4c744966dd3de5a05246cf098601";

  outputs = { self, nixpkgs, nixpkgs-qt612 }:
    let
      forAllSystems = nixpkgs.lib.genAttrs [ "x86_64-linux" "aarch64-linux" ];
      buildSource =
        let workspaceRoot = builtins.getEnv "PWD";
        in if workspaceRoot == "" then self else builtins.path {
          path = workspaceRoot;
          name = "kde-bluetooth-smartlocker-source";
          filter = path: type:
            !(builtins.elem (baseNameOf (toString path)) [
              ".git"
              ".omo"
              ".codegraph"
              "build"
              "result"
              "result-appimage"
            ]);
        };

      # Build the package against a caller-supplied nixpkgs. The NixOS module
      # passes the consuming system's `pkgs` so the native QML plugin is built
      # against the same Qt as the user's Plasma and cannot hit a Qt
      # minor-version ABI mismatch.
      mkPackage = pkgs: pkgs.stdenv.mkDerivation {
        pname = "kde-bluetooth-smartlocker";
        version = "0.19.0";
        src = self;
        # dbus is needed by the test suite, which runs a private message bus so BlueZ
        # signals are actually routed rather than dropped by a peer-to-peer endpoint.
        nativeBuildInputs = [ pkgs.cmake pkgs.ninja pkgs.dbus pkgs.kdePackages.kirigami.unwrapped pkgs.qt6.wrapQtAppsHook ];
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
            -I "${pkgs.kdePackages.kirigami.unwrapped}/lib/qt-6/qml" \
            -I "${pkgs.kdePackages.libplasma}/lib/qt-6/qml" \
            -I "${pkgs.quickshell}/lib/qt-6/qml" \
            -I plasmoid/qml \
            plasmoid/contents/ui/main.qml \
            plasmoid/contents/ui/DevicePolicyRow.qml \
            examples/quickshell/shell.qml > qmllint.log 2>&1
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

      mkPlasmaVariant = pkgs: qtVersion: let
        readPluginTest = file:
          let
            trackedFile = "${self.outPath}/${file}";
            workspaceRoot = builtins.getEnv "PWD";
            workspaceFile = "${workspaceRoot}/${file}";
          in
          if builtins.pathExists trackedFile then builtins.readFile trackedFile
          else if workspaceRoot != "" && builtins.pathExists workspaceFile then builtins.readFile workspaceFile
          else throw "Missing ${file} from the flake source. Track the fixture for pure evaluation, or run local evaluation with --impure from the workspace root.";
      in pkgs.stdenv.mkDerivation {
        pname = "kde-bluetooth-smartlocker-plasma-qt6_11";
        version = "0.19.0";
        src = pkgs.lib.cleanSourceWith {
          src = ./.;
          filter = path: type:
            !(builtins.elem (baseNameOf (toString path)) [
              ".git"
              ".omo"
              ".codegraph"
              "build"
              "result"
            ]);
        };
        dontWrapQtApps = true;
        nativeBuildInputs = [ pkgs.cmake pkgs.ninja pkgs.patchelf ];
        buildInputs = [ pkgs.qt6.qtbase pkgs.qt6.qtdeclarative ];
        doCheck = true;
        doInstallCheck = true;
        postPatch = ''
          cat > tests/qml-plugin-smoke.sh <<'SMARTLOCKER_QML_RUNNER'
          ${readPluginTest "tests/qml-plugin-smoke.sh"}
          SMARTLOCKER_QML_RUNNER
          cat > tests/test-qml-plugin-selector.sh <<'SMARTLOCKER_QML_SELECTOR_TEST'
          ${readPluginTest "tests/test-qml-plugin-selector.sh"}
          SMARTLOCKER_QML_SELECTOR_TEST
          cat > tests/qml-plugin-smoke.qml <<'SMARTLOCKER_QML_SMOKE'
          ${readPluginTest "tests/qml-plugin-smoke.qml"}
          SMARTLOCKER_QML_SMOKE
        '';
        configurePhase = ''
          runHook preConfigure
          cmake -S . -B build -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_TESTING=OFF \
            -DCMAKE_INSTALL_PREFIX="$out"
          runHook postConfigure
        '';
        buildPhase = ''
          runHook preBuild
          cmake --build build --target smartlocker-qml-plugin
          runHook postBuild
        '';
        checkPhase = ''
          runHook preCheck
          bash tests/test-qml-plugin-selector.sh
          runHook postCheck
        '';
        installPhase = ''
          runHook preInstall
          module="$out/lib/qt-6/qml/org/kde/smartlocker"
          mkdir -p "$module"
          cp build/libsmartlockerqml.so "$module/"
          cp plasmoid/qml/org/kde/smartlocker/qmldir "$module/"
          printf '%s\n' "${qtVersion}" > "$module/qt-version"
          runHook postInstall
        '';
        postFixup = ''
          plugin="$out/lib/qt-6/qml/org/kde/smartlocker/libsmartlockerqml.so"
          patchelf --remove-rpath "$plugin"
          if [ -n "$(patchelf --print-rpath "$plugin")" ]; then
            echo "Qt plugin still has an ELF RPATH/RUNPATH" >&2
            exit 1
          fi
          if patchelf --print-needed "$plugin" | grep -Fq '/nix/store/'; then
            echo "Qt plugin has a Nix-store dynamic dependency" >&2
            exit 1
          fi
        '';
        installCheckPhase = ''
          runHook preInstallCheck
          bash tests/qml-plugin-smoke.sh "${qtVersion}" \
            "$out/lib/qt-6/qml/org/kde/smartlocker" \
            "${pkgs.qt6.qtdeclarative}/bin/qml"
          runHook postInstallCheck
        '';
        passthru = {
          inherit qtVersion;
          qtBase = pkgs.qt6.qtbase;
          qtDeclarative = pkgs.qt6.qtdeclarative;
        };
      };

      mkAppImage = system:
        let
          appVersion = "0.19.0";
          pkgs = import nixpkgs-qt612 { inherit system; };
          hostPkgs = import nixpkgs { inherit system; };
          daemon = (mkPackage pkgs).overrideAttrs (old: {
            src = buildSource;
            dontWrapQtApps = true;
            nativeBuildInputs = builtins.filter (input: input != pkgs.qt6.wrapQtAppsHook) old.nativeBuildInputs;
          });
          linuxdeploy = pkgs.fetchurl {
            url = "https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage";
            hash = "sha256-wgzXHjpOO4DDSDzveTzaP06ZCsoUAU0jxUTKPOEnC00=";
          };
          linuxdeployQt = pkgs.fetchurl {
            url = "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage";
            hash = "sha256-FRBr6IXBxIoCEZjn4emkjOnQKobdChhI8Avb88HJJyQ=";
          };
          appimagetool = pkgs.fetchurl {
            url = "https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage";
            hash = "sha256-7UzoTw2cr/ZvULzKb/bzWq5UzoE1QIs/ozq/w8s4TrA=";
          };
          appimageRuntime = pkgs.fetchurl {
            url = "https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64";
            hash = "sha256-L8qLRDySUQ8Ug6iD9gBhrQm0a5eLJjHIB82HOkfsJg0=";
          };
        in pkgs.stdenvNoCC.mkDerivation {
          pname = "kde-bluetooth-smartlocker-appimage";
          version = appVersion;
          src = buildSource;
          nativeBuildInputs = [ pkgs.bash pkgs.coreutils pkgs.findutils pkgs.gnugrep pkgs.file pkgs.glibc.bin pkgs.patchelf ];
          dontUnpack = true;
          dontFixup = true;
          buildPhase = ''
            runHook preBuild
            export APPIMAGE_DAEMON="${daemon}"
            export APPIMAGE_HOST_PLUGIN="${mkPlasmaVariant hostPkgs "6.11.1"}"
            export APPIMAGE_LINUXDEPLOY="${linuxdeploy}"
            export APPIMAGE_QT_PLUGIN="${linuxdeployQt}"
            export APPIMAGE_TOOL="${appimagetool}"
            export APPIMAGE_RUNTIME="${appimageRuntime}"
            export APPIMAGE_QMAKE="${pkgs.qt6.qtbase}/bin/qmake"
            export APPIMAGE_ZLIB="${pkgs.zlib}/lib/libz.so.1"
            export APPIMAGE_LIBSTDCXX="${pkgs.stdenv.cc.cc.lib}/lib/libstdc++.so.6"
            export APPIMAGE_LIBGCC="${pkgs.stdenv.cc.cc.lib}/lib/libgcc_s.so.1"
            export APPIMAGE_GLIBC="${pkgs.glibc}"
            export APPIMAGE_VERSION="${appVersion}"
            export APPIMAGE_OUT="$PWD/out"
            bash "$src/scripts/package-appimage.sh"
            runHook postBuild
          '';
          installPhase = ''
            mkdir -p "$out"
            cp out/*.AppImage out/*.sha256 "$out/"
            test "$(find "$out" -maxdepth 1 -name '*.AppImage' | wc -l)" -eq 1
          '';
        };

    in {
      packages = forAllSystems (system: {
        default = mkPackage (import nixpkgs { inherit system; });
        # Reuse the locked default package set, whose Qt 6.11.1 paths match the
        # installed host runtime. The service/NixOS default above remains the
        # consuming-pkgs package for system ABI compatibility.
        plasma-qt6_11 = mkPlasmaVariant (import nixpkgs { inherit system; }) "6.11.1";
      } // nixpkgs.lib.optionalAttrs (system == "x86_64-linux") {
        appimage = mkAppImage system;
      });

      devShells = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
        in {
          default = pkgs.mkShell {
            packages = [
              pkgs.cmake
              pkgs.actionlint
              pkgs.gcc
              pkgs.ninja
              pkgs.python3Packages.pyyaml
              pkgs.shellcheck
              pkgs.zsh
              pkgs.qt6.qtbase
              pkgs.qt6.qtdeclarative
              pkgs.qt6.qttools
              pkgs.kdePackages.plasma-sdk
              pkgs.kdePackages.kirigami.unwrapped
              pkgs.kdePackages.libplasma
              pkgs.kdePackages.plasma-desktop
              pkgs.quickshell
            ];
            # NOTE: qmllint cannot auto-discover QML import paths inside `nix develop`
            # (nixpkgs issue #31725). The explicit QML_IMPORT_PATH below is the workaround.
            # Remove this hook when that issue is resolved upstream.
            shellHook = ''
              # qtdeclarative 6.11.1 (nixos-unstable) moved qmlimportscanner from
              # bin/ to libexec/; libexec is not on the mkShell PATH by default.
              # Add both libexec and bin to support different Qt packaging layouts.
              export PATH="${pkgs.qt6.qtdeclarative}/libexec:${pkgs.qt6.qtdeclarative}/bin:$PATH"
              export QML_IMPORT_PATH="${pkgs.qt6.qtdeclarative}/lib/qt-6/qml:${pkgs.kdePackages.kirigami.unwrapped}/lib/qt-6/qml:${pkgs.kdePackages.libplasma}/lib/qt-6/qml:${pkgs.quickshell}/lib/qt-6/qml"
              export QML2_IMPORT_PATH="${pkgs.kdePackages.plasma-desktop}/lib/qt-6/qml:${pkgs.qt6.qtdeclarative}/lib/qt-6/qml:${pkgs.kdePackages.kirigami.unwrapped}/lib/qt-6/qml:${pkgs.kdePackages.libplasma}/lib/qt-6/qml:${pkgs.quickshell}/lib/qt-6/qml"
              export QT_PLUGIN_PATH="${pkgs.qt6.qtbase}/lib/qt-6/plugins"
            '';
          };
        });

      nixosModules.default = { config, lib, pkgs, utils, ... }:
        let
          cfg = config.services.kdeBluetoothSmartlocker;
          # Restrict device specs to what the daemon actually accepts so that nothing
          # can terminate the argument or smuggle whitespace past the ExecStart list.
          validDeviceSpec = spec:
            builtins.match "([0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}|[0-9A-Fa-f]{12}|/org/bluez/hci[0-9]+/dev_[0-9A-Fa-f]{2}(_[0-9A-Fa-f]{2}){5}"
              spec != null;
        in {
          options.services.kdeBluetoothSmartlocker = {
            enable = lib.mkEnableOption "the KDE Bluetooth SmartLocker user service";
            package = lib.mkOption {
              type = lib.types.package;
              # Build against the consuming system's nixpkgs so the native QML
              # plugin matches the user's Plasma/Qt minor version.
              default = mkPackage pkgs;
            };
            installDmsPlugin = lib.mkOption {
              type = lib.types.bool;
              default = true;
              description = ''
                Install the DankMaterialShell (DMS) plugin system-wide at
                /etc/xdg/quickshell/dms-plugins/bluetoothSmartLocker, where DMS
                1.6.2 discovers plugins. DMS hardcodes /etc/xdg/quickshell/dms-plugins
                and does not search XDG_CONFIG_DIRS. Set this to false when the same
                plugin is installed through DMS's own home-manager or NixOS plugin
                option, so the two installs do not collide under /etc.
              '';
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
            environment.etc = lib.mkIf cfg.installDmsPlugin {
              "xdg/quickshell/dms-plugins/bluetoothSmartLocker".source =
                "${cfg.package}/share/kde-bluetooth-smartlocker/dms-plugin";
            };
            assertions = [
              {
                assertion = builtins.all validDeviceSpec cfg.devices;
                message = "kdeBluetoothSmartlocker device specs must be Bluetooth addresses (AA:BB:CC:DD:EE:FF, AA-BB-CC-DD-EE-FF, AABBCCDDEEFF) or legacy BlueZ object paths (/org/bluez/hciX/dev_XX_XX_XX_XX_XX_XX): ${lib.concatMapStringsSep ", " (p: ''"${p}"'') (lib.filter (p: !(validDeviceSpec p)) cfg.devices)}";
              }
            ];
            systemd.user.services.kde-bluetooth-smartlocker = {
              description = "KDE Bluetooth SmartLocker";
              wantedBy = [ "graphical-session.target" ];
              unitConfig = {
                StartLimitIntervalSec = "10min";
                StartLimitBurst = 10;
              };
              # serviceConfig renders each LIST element as its own ExecStart= line, and a
              # Type=simple unit allows only one, so this must stay a single escaped string.
              serviceConfig.ExecStart = utils.escapeSystemdExecArgs (
                [
                  "${cfg.package}/bin/kde-bluetooth-smartlocker"
                  "--lock-command"
                  "${pkgs.systemd}/bin/loginctl"
                  "--away-seconds"
                  (toString cfg.awaySeconds)
                  "--snooze-seconds"
                  (toString cfg.snoozeSeconds)
                  "--resume-grace-seconds"
                  (toString cfg.resumeGraceSeconds)
                  "--minimum-present"
                  (toString cfg.minimumPresent)
                  "--rssi-threshold"
                  (toString cfg.rssiThreshold)
                  "--rssi-hysteresis"
                  (toString cfg.rssiHysteresis)
                  "--rssi-samples"
                  (toString cfg.rssiSamples)
                ]
                ++ lib.optional cfg.prelockNotify "--prelock-notify"
                ++ lib.concatMap (device: [ "--device" device ]) cfg.devices
              );
              serviceConfig.Restart = "on-failure";
              serviceConfig.RestartSec = "2";
              serviceConfig.NoNewPrivileges = true;
              serviceConfig.CapabilityBoundingSet = "";
              serviceConfig.ProtectSystem = "strict";
              serviceConfig.ProtectHome = "read-only";
              serviceConfig.ProtectKernelTunables = true;
              serviceConfig.ProtectControlGroups = true;
              serviceConfig.RestrictSUIDSGID = true;
              serviceConfig.RestrictRealtime = true;
              serviceConfig.LockPersonality = true;
              serviceConfig.MemoryDenyWriteExecute = true;
              serviceConfig.RestrictAddressFamilies = [ "AF_UNIX" "AF_NETLINK" ];
              serviceConfig.ReadWritePaths = [ "%h/.config/kde-bluetooth-smartlocker" ];
            };
          };
        };
    };
}
