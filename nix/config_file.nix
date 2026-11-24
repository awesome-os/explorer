{
  pkgs,
  settings,
  package ? null,
  checkConfig ? true,
}:
let
  rawConfig =
    if builtins.isPath settings then
      settings
    else if builtins.isString settings then
      pkgs.writeText "luciole-config.toml" settings
    else
      (pkgs.formats.toml { }).generate "luciole-config.toml" settings;
in
if checkConfig && package != null then
  pkgs.runCommand "luciole-config.toml" { } ''
    ${pkgs.lib.escapeShellArg (pkgs.lib.getExe package)} config validate ${pkgs.lib.escapeShellArg "${rawConfig}"}
    cp ${pkgs.lib.escapeShellArg "${rawConfig}"} "$out"
  ''
else
  rawConfig
