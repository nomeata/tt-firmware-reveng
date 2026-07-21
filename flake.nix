{
  description = "tt-firmware-reveng — reproducible reverse-engineering pipeline for tiptoi pen firmware";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.11";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; config.allowUnfree = true; };

        # Python for the tooling: stdlib covers fetch/unpack; capstone drives the
        # static-analysis helpers (fw.py / xref.py); pyyaml is used by the test harness.
        python = pkgs.python3.withPackages (ps: [
          ps.capstone
          ps.pyyaml
        ]);

        # pkgs.ghidra provides `ghidra-analyzeHeadless` (the exact command the pipeline
        # calls) and pulls in a matching JDK. The naming/signature databases were authored
        # against Ghidra 11.x; pinning nixpkgs pins the Ghidra version too (here 11.2.1).
        ghidra = pkgs.ghidra;
      in
      {
        devShells.default = pkgs.mkShell {
          packages = [
            ghidra
            python
            pkgs.coreutils
            pkgs.curl
            pkgs.cacert
            pkgs.git
            pkgs.jq
          ];

          shellHook = ''
            export SSL_CERT_FILE=${pkgs.cacert}/etc/ssl/certs/ca-bundle.crt
            echo "tt-firmware-reveng dev shell"
            echo "  ghidra: $(command -v ghidra-analyzeHeadless || echo 'NOT FOUND')"
            echo "  python: $(python3 --version)"
            echo
            echo "Reproduce the flagship variant:"
            echo "  tools/fetch_firmware.py 2N-update3202MT   # download + verify + unpack"
            echo "  FW=fw/2N-update3202MT tools/make_base.sh   # one-time Ghidra base (~minutes)"
            echo "  FW=fw/2N-update3202MT tools/regen.sh       # -> fw/.../out/decomp_named/"
          '';
        };
      });
}
