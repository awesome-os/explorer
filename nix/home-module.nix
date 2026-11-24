{
  config,
  pkgs,
  lib,
  ...
}:
let
  cfg = config.programs.luciole;
  toml = pkgs.formats.toml { };
in
{
  _class = "homeManager";

  options.programs.luciole = {
    enable = lib.mkEnableOption "Luciole";
    package = lib.mkOption {
      type = lib.types.nullOr lib.types.package;
      default = null;
      description = "Luciole package to install.";
    };
    settings = lib.mkOption {
      type = lib.types.oneOf [
        toml.type
        lib.types.str
        lib.types.path
      ];
      default = { };
      description = "Base TOML settings as an attribute set, TOML string, or file path. Browser defaults are separate from writable GUI overrides.";
    };
    checkConfig = lib.mkOption {
      type = lib.types.bool;
      default = true;
      description = "Validate the base configuration at build time when a package is selected.";
    };
  };

  config = lib.mkIf cfg.enable {
    home.packages = lib.optional (cfg.package != null) cfg.package;
    xdg.configFile."luciole/config.toml" = lib.mkIf (cfg.settings != { }) {
      source = import ./config_file.nix {
        inherit pkgs;
        inherit (cfg) settings package checkConfig;
      };
    };
  };
}
