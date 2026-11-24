{ pkgs, package }:
let
  inherit (pkgs) lib;
  settings = {
    schema_version = 1;
    browser = {
      show_hidden = true;
      sort_by = "size";
      sort_descending = true;
      directories_first = false;
    };
  };
  text = builtins.readFile ../tests/fixtures/settings.toml;
  # Exercise module wiring through the framework interfaces without fetching
  # Home Manager or Hjem. Full consumer-framework integration is separate.
  evaluate =
    kind: values:
    let
      home = kind == "home";
      evaluated = lib.evalModules {
        class = if home then "homeManager" else "hjem";
        specialArgs = { inherit pkgs; };
        modules = [
          (if home then ./home-module.nix else ./hjem-module.nix)
          {
            options =
              if home then
                {
                  home.packages = lib.mkOption {
                    type = lib.types.listOf lib.types.package;
                    default = [ ];
                  };
                  xdg.configFile = lib.mkOption {
                    type = lib.types.attrsOf lib.types.anything;
                    default = { };
                  };
                }
              else
                {
                  packages = lib.mkOption {
                    type = lib.types.listOf lib.types.package;
                    default = [ ];
                  };
                  xdg.config.files = lib.mkOption {
                    type = lib.types.attrsOf lib.types.anything;
                    default = { };
                  };
                };
          }
          {
            programs.luciole = {
              enable = true;
              inherit package settings;
            }
            // values;
          }
        ];
      };
      inherit (evaluated) config;
    in
    {
      source =
        if home then
          config.xdg.configFile."luciole/config.toml".source
        else
          config.xdg.config.files."luciole/config.toml".source;
      inherit (config.programs.luciole) checkConfig;
      packages = if home then config.home.packages else config.packages;
    };
  checkedHome = evaluate "home" { };
  uncheckedHome = evaluate "home" { checkConfig = false; };
  invalid = "unsupported = true\n";
  skipped = evaluate "home" {
    checkConfig = false;
    settings = invalid;
  };
  withoutPackage = evaluate "hjem" {
    package = null;
    settings = invalid;
  };
  checked =
    lib.concatMap
      (kind: [
        (evaluate kind { }).source
        (evaluate kind { settings = text; }).source
        (evaluate kind { settings = ../tests/fixtures/settings.toml; }).source
      ])
      [
        "home"
        "hjem"
      ];
in
assert checkedHome.checkConfig;
assert checkedHome.source.drvPath != uncheckedHome.source.drvPath;
assert builtins.length checkedHome.packages == 1;
assert withoutPackage.packages == [ ];
pkgs.runCommand "luciole-module-config-check" { nativeBuildInputs = [ pkgs.python3 ]; } ''
  python3 - ${lib.escapeShellArgs (map (file: "${file}") checked)} <<'PY'
  import sys
  import tomllib
  from pathlib import Path

  expected = {
      "schema_version": 1,
      "browser": {"show_hidden": True, "sort_by": "size", "sort_descending": True, "directories_first": False},
  }
  for name in sys.argv[1:]:
      with Path(name).open("rb") as source:
          assert tomllib.load(source) == expected, name
  PY
  cmp ${lib.escapeShellArg "${skipped.source}"} ${lib.escapeShellArg "${withoutPackage.source}"}
  mkdir "$out"
  touch "$out/passed"
''
