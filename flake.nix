{
  description = "Rev framework and LithoRev Linux build";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
      pkgsFor = system: import nixpkgs { inherit system; };
    in
    {
      packages = forAllSystems (system:
        let
          pkgs = pkgsFor system;
          llvm = pkgs.llvmPackages_18;
        in
        {
          default = llvm.stdenv.mkDerivation {
            pname = "lithorev";
            version = "0-unstable";
            src = pkgs.lib.cleanSourceWith {
              src = ./.;
              filter = path: type:
                let
                  relative = pkgs.lib.removePrefix "${toString ./.}/" (toString path);
                  topLevel = !(pkgs.lib.hasInfix "/" relative);
                in
                !(type == "directory" && topLevel
                  && (relative == ".git" || pkgs.lib.hasPrefix "build" relative));
            };

            nativeBuildInputs = with pkgs; [
              cmake
              ninja
              python3
              makeWrapper
              llvm.clang-tools
            ];

            buildInputs = with pkgs; [
              freetype
              glew
              libGL
              libx11
              libxext
              libxrandr
            ];

            cmakeFlags = [
              "-DCMAKE_BUILD_TYPE=Release"
              "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${llvm.clang-tools}/bin/clang-scan-deps"
            ];

            installPhase = ''
              runHook preInstall
              mkdir -p "$out/bin"
              cp Demo/LithoRev "$out/bin/LithoRev"
              wrapProgram "$out/bin/LithoRev" \
                --prefix PATH : ${pkgs.lib.makeBinPath [ pkgs.xrandr pkgs.xclip pkgs.zenity ]}
              runHook postInstall
            '';

            meta.mainProgram = "LithoRev";
          };
        });

      devShells = forAllSystems (system:
        let
          pkgs = pkgsFor system;
          llvm = pkgs.llvmPackages_18;
        in
        {
          default = pkgs.mkShell.override { stdenv = llvm.stdenv; } {
            packages = with pkgs; [
              cmake
              ninja
              python3
              llvm.clang-tools
              freetype
              glew
              libGL
              libx11
              libxext
              libxrandr
              xrandr
              xclip
              zenity
            ];
          };
        });
    };
}
