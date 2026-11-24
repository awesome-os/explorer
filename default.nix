let
  lock = builtins.fromJSON (builtins.readFile ./flake.lock);
  locked = lock.nodes.${lock.nodes.root.inputs.nixpkgs}.locked;
  nixpkgs = builtins.fetchTarball {
    inherit (locked) url;
    sha256 = locked.narHash;
  };
in
{
  pkgs ? import nixpkgs { },
}:
let
  package = pkgs.callPackage ./nix/package.nix { };
  withPackage = module: {
    imports = [ module ];
    programs.luciole.package = pkgs.lib.mkDefault package;
  };
in
{
  inherit package;
  homeModule = withPackage ./nix/home-module.nix;
  hjemModule = withPackage ./nix/hjem-module.nix;
  nixosModule = withPackage ./nix/nixos-module.nix;
}
