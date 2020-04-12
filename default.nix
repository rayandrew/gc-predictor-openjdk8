with import <nixpkgs> {};

let
  pname   = "gc-predictor-openjdk8";
  version = "0.0.1";
  stdenv  = gcc5Stdenv;
in
stdenv.mkDerivation rec {
  name = pname;
  src = if lib.inNixShell then null else ./.;
  # src  = builtins.filterSource (p: t: lib.cleanSourceFilter p t && baseNameOf p != "OBF_DROP_DIR" && baseNameOf p != "build") ./.;

  nativeBuildInputs = [
    stdenv
    pkgconfig
    ccache
    unzip
    autoconf
    file
    zip
    curl
    subversion
    mercurial
    openssl
    bash
    cacert
    ant
  ];

  buildInputs = [
    openjdk8

    # libraries
    coreutils
    cpio
    which
    alsaLib
    cups
    xlibs.libXaw
    xlibs.libXfont2
    xlibs.libXrender
    xlibs.libXft
    xlibs.libXres
    xlibs.libICE
    xlibs.libXcomposite
    xlibs.libXi
    xlibs.libXt
    xlibs.libSM
    xlibs.libXcursor
    xlibs.libXtst
    xlibs.libXmu
    xlibs.libXv
    xlibs.libX11
    xlibs.libXext
    xlibs.libXpresent
    xlibs.libXrandr
  ];

  OBF_JAVA11_HOME = "${openjdk8.out}/lib/openjdk";
  SSL_CERT_FILE   = "${cacert}/etc/ssl/certs/ca-bundle.crt";

  enableParallelBuilding = true;
  outputs = [ "out" ];

  # preConfigure           = ''

  # '';

  # prebuild               = ''

  # '';

  buildPhase             = ''
    export SSL_CERT_FILE=${cacert}/etc/ssl/certs/ca-bundle.crt
    chmod +x build.sh
    bash build.sh
  '';

  installPhase           = ''
    cp -r ./sources/openjdk8/build/linux-x86_64-normal-server-release/images/j2sdk-image $out
  '';
  
  # makeTarget             = "gc-predictor";

  # doCheck                = false;
  # checkTarget            = "test";
}
