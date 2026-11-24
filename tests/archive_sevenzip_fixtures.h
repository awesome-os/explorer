#pragma once

#include <array>
#include <string_view>

// Own payloads encoded by 7-Zip 26.04, with timestamps omitted. These reference
// fixtures cover providers the libarchive writer cannot generate (notably AES).
// The two encrypted fixtures use the public test-only password fixture-pass.
namespace sevenzip_fixtures {
  struct Fixture {
    std::string_view name, base64;
    bool encrypted;
  };
  inline constexpr std::array archives{
      Fixture{
          "lzma2",
          "N3q8ryccAASaBuANmAAAAAAAAAAhAAAAAAAAAJnRV33gA58AIV0AMxpKrAxyv5zanmNHqsOZmeDDqYxL6ZAnJ2yd2V+/0oAAAAAA"
          "gTMHrg/QluR8nz9HQQQPcQ0SU4hghjAKn1eiUtsAXOYo2A2V3IcqCWLKvmO62WJnXisETLhTcFG2GtrXtA092eWE0lMBOBKEYbkU"
          "1S0yimodr/0T6INZ8b8jKaqYKph/xyRtNN4ds4N/kGPQABcGKQEJbwAHCwEAASMDAQEFXQAQAAAMgJoKAThXT8EAAA==",
          false
      },
      Fixture{
          "lzma1",
          "N3q8ryccAASYjR/tIgAAAAAAAACaAAAAAAAAAK2Yh14AMxpKrAxyv5zanmNHqsOZmeDDqYxL6ZAnJ2yd2V+/0oAAAQQGAAEJIgAH"
          "CwEAASMDAQEFXQAQAAAMg6AACA0CCYHACgEdZMSvBFRxFAAABQMOAYAZCQAAAAAAAAAAABFHAGYAbwBsAGQAZQByAAAAZgBvAGwA"
          "ZABlAHIALwBmAGkAcgBzAHQALgB0AHgAdAAAAHMAZQBjAG8AbgBkAC4AdAB4AHQAAAAZABUOAQAQgO1BIICkgSCApIEAAA==",
          false
      },
      Fixture{
          "copy",
          "N3q8ryccAAR9hrF6oAMAAAAAAACKAAAAAAAAABQU3W9maXJzdCBwYXlsb2FkCmZpcnN0IHBheWxvYWQKZmlyc3QgcGF5bG9hZApm"
          "aXJzdCBwYXlsb2FkCmZpcnN0IHBheWxvYWQKZmlyc3QgcGF5bG9hZApmaXJzdCBwYXlsb2FkCmZpcnN0IHBheWxvYWQKZmlyc3Qg"
          "cGF5bG9hZApmaXJzdCBwYXlsb2FkCmZpcnN0IHBheWxvYWQKZmlyc3QgcGF5bG9hZApmaXJzdCBwYXlsb2FkCmZpcnN0IHBheWxv"
          "YWQKZmlyc3QgcGF5bG9hZApmaXJzdCBwYXlsb2FkCmZpcnN0IHBheWxvYWQKZmlyc3QgcGF5bG9hZApmaXJzdCBwYXlsb2FkCmZp"
          "cnN0IHBheWxvYWQKZmlyc3QgcGF5bG9hZApmaXJzdCBwYXlsb2FkCmZpcnN0IHBheWxvYWQKZmlyc3QgcGF5bG9hZApmaXJzdCBw"
          "YXlsb2FkCmZpcnN0IHBheWxvYWQKZmlyc3QgcGF5bG9hZApmaXJzdCBwYXlsb2FkCmZpcnN0IHBheWxvYWQKZmlyc3QgcGF5bG9h"
          "ZApmaXJzdCBwYXlsb2FkCmZpcnN0IHBheWxvYWQKc2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQK"
          "c2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQK"
          "c2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQK"
          "c2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQK"
          "c2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQK"
          "c2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQK"
          "c2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQKc2Vjb25kIHBheWxvYWQKAQQGAAIJgcCB4AAHCwIA"
          "AQEAAQEADIHAgeAACAoBHWTErwRUcRQAAAUDDgGAEUcAZgBvAGwAZABlAHIAAABmAG8AbABkAGUAcgAvAGYAaQByAHMAdAAuAHQA"
          "eAB0AAAAcwBlAGMAbwBuAGQALgB0AHgAdAAAABkAFQ4BABCA7UEggKSBIICkgQAA",
          false
      },
      Fixture{
          "deflate",
          "N3q8ryccAARZqY+uKQAAAAAAAACaAAAAAAAAAKiOYWvtx8EJACAIAMB/U7SaZEEQKerH7Z1D8X53rqhNBn8EOHrZpnvRx27VBgEE"
          "BgABCSkABwsBAAEDBAEIDIOgAAgNAgmBwAoBHWTErwRUcRQAAAUDDgGAGQ8AAAAAAAAAAAAAAAAAAAARRwBmAG8AbABkAGUAcgAA"
          "AGYAbwBsAGQAZQByAC8AZgBpAHIAcwB0AC4AdAB4AHQAAABzAGUAYwBvAG4AZAAuAHQAeAB0AAAAGQAVDgEAEIDtQSCApIEggKSB"
          "AAA=",
          false
      },
      Fixture{
          "bzip2",
          "N3q8ryccAAR6s/4RTwAAAAAAAACaAAAAAAAAAOBEtrxCWmgxMUFZJlNZY3Jr/wAA0FGAABBAAC8l3CAgAHBQwAAKlIGRgnUBqSSk"
          "klLgYAsCoGAaBYFwbAsDQGQZA0CoNg9BcD8XckU4UJBjcmv/AQQGAAEJTwAHCwEAAQMEAgIMg6AACA0CCYHACgEdZMSvBFRxFAAA"
          "BQMOAYAZDwAAAAAAAAAAAAAAAAAAABFHAGYAbwBsAGQAZQByAAAAZgBvAGwAZABlAHIALwBmAGkAcgBzAHQALgB0AHgAdAAAAHMA"
          "ZQBjAG8AbgBkAC4AdAB4AHQAAAAZABUOAQAQgO1BIICkgSCApIEAAA==",
          false
      },
      Fixture{
          "ppmd",
          "N3q8ryccAATss4QnKQAAAAAAAACaAAAAAAAAACcbhssAZgVYJBVmsk1GACvWo1KOZeYAAiD7LYjJqMUC2EJPhycNgKZFdsX4AAEE"
          "BgABCSkABwsBAAEjAwQBBQQAAAEADIOgAAgNAgmBwAoBHWTErwRUcRQAAAUDDgGAGQkAAAAAAAAAAAARRwBmAG8AbABkAGUAcgAA"
          "AGYAbwBsAGQAZQByAC8AZgBpAHIAcwB0AC4AdAB4AHQAAABzAGUAYwBvAG4AZAAuAHQAeAB0AAAAGQAVDgEAEIDtQSCApIEggKSB"
          "AAA=",
          false
      },
      Fixture{
          "nonsolid",
          "N3q8ryccAAQWC7z9PwAAAAAAAACaAAAAAAAAAOidsrzgAb8AF10AMxpKrAxyv5zanmNHqsOZmeDDqYOnAAAA4AHfABhdADmZSJGx"
          "aZYH4/a/A/etw23LPAFoihAAAAABBAYAAgkfIAAHCwIAASEhAQABISEBAAyBwIHgAAgKAR1kxK8EVHEUAAAFAw4BgBkMAAAAAAAA"
          "AAAAAAAAEUcAZgBvAGwAZABlAHIAAABmAG8AbABkAGUAcgAvAGYAaQByAHMAdAAuAHQAeAB0AAAAcwBlAGMAbwBuAGQALgB0AHgA"
          "dAAAABkAFQ4BABCA7UEggKSBIICkgQAA",
          false
      },
      Fixture{
          "encrypted-data",
          "N3q8ryccAARWrfqJMAAAAAAAAACqAAAAAAAAAORu3Ki/ksRyd3wrtTJ636zhxykWaJd5d8EqCW9bjR1ZsQIqrxVKhq+WgOUEES5V"
          "sPKZIlEBBAYAAQkwAAcLAQACJAbxBwESUw+vQUsnZqbTgeB5vYT4e5dHISEBAAEADCmDoAAIDQIJgcAKAR1kxK8EVHEUAAAFAw4B"
          "gBkEAAAAABFHAGYAbwBsAGQAZQByAAAAZgBvAGwAZABlAHIALwBmAGkAcgBzAHQALgB0AHgAdAAAAHMAZQBjAG8AbgBkAC4AdAB4"
          "AHQAAAAZABUOAQAQgO1BIICkgSCApIEAAA==",
          true
      },
      Fixture{
          "encrypted-header",
          "N3q8ryccAAS/Fryr4AAAAAAAAAAwAAAAAAAAAHOkGoicyqz8xmOa2Wq7slKU0m/wsM2U0pig9Xcl10wk/Ul7hPMQwoeNP7jZFZDJ"
          "9lQUWej35Tczt1zhlxHieQ3T7D+I6mRSGHuHv9s01QgOX9Uw6dACTaA6HBoQ33YjJZN9cgDVakFHgbjqAfif5Rd55u4hTVwgAmUq"
          "7FgtIVjIFZrRGkVmvL0FmW4wd9120s5CskPUg5npqMDDJlmbLSVKoV3Rwfj6I9OSqqaWw2lmZQER6BNlVLVIBtk9+KHZOQLQmMVs"
          "Y6TEfss2TzR+36WPiElK3p35t98DiYH4NKLaaHmxPRcGMAEJgLAABwsBAAEkBvEHARJTDxySV3WLfftaiGHTMoAllz0MgKoKAR7I"
          "uH0AAA==",
          true
      },
  };
  // Literal names and the public test-only password " café secret ".
  inline constexpr Fixture literal{
      "literal",
      "N3q8ryccAAS+w6yyKQEAAAAAAAAiAAAAAAAAAOZuv+jhBHz7UlGZJ3dcGWqG+cj29nf6TRTWANKrqJ46PlrrpMoUTiJdLPHOFbPl"
      "SmLUtVJnlaOaJLbuu3LLoiaL/s1EpnEfxPSLIUQc8ZxNM9kQMAAAgTMHrg/SwW/9QLuUZDs5pHrnaiJtKa/oGNAT7Q4VJwWbL/vV"
      "BAJM5yz/H5fF+NoMZju0SO11o9bYS2AqZkal/JU3uJZElVDyZHMPD0gJ3opW/SFnDOxJTg0fWZEH7hboIGZqEPxEpPCiYiZaDa9Z"
      "084cAo9AuaDQ1L97hjB4nhX+BPRujne9SiffDuisiHeipi1jjuC5mmEQGoHSguI7SPzSUad5h1SX529q2/HapKqGwWcgj9uUjVD6"
      "/2Qoh1Ba79YKzDpQ2GTbudoHObgfBkXU+AAAAAAXBlABCYDZAAcLAQABIwMBAQVdABAAAAyBOgoBlCmeCgAA",
      true
  };

  // Header-encrypted own payloads: 7-Zip 26.04 and py7zr 1.1.3.
  inline constexpr Fixture literalHeader{
      "literalHeader",
      "N3q8ryccAATmRBq+MAEAAAAAAAA+AAAAAAAAAFFAnDMwpgl2L1w25XgQrTIGXOCJBtzigddjfiDo6LUsah3IYPBVhWkZcg3Aajdu"
      "EDxJOGkw1t+Z9yS9b1dmbMrvCSSOkU4Q3+VdiV7VrjAknyskaC9HAhevwoT7Q4H2nbezAFPhOWbzENXxL08p9WiSvgPuGAFxAs9z"
      "FRNvKWY+/5jwM9XLVSlosSYyS/UdxB5tHd93ged8DHIh8+oSrlEtaFE4ns6bmy7SZKv3tKa996/DnWfrXcphOiYo35aVPzAUm/oC"
      "Ci1QpJFMsKsvz/jx71c5o7qZPOD7BspWcZMjzKV47wdzafa6oxkQTrJk09zTO38SSmPNAGd6hDAZMsK8FBze2sYQc/8F1+syep2J"
      "vUeT5r2Aa9CAQhR6OHZqZG6nK0YaUs9yGZhdeEcba7q5ll6MFwZQAQmA4AAHCwEAAiQG8QcBElMPEWZ75inmDy25E/jalGNjBiMD"
      "AQEFXQAQAAABAAyA2IE6CgGjnEksAAA=",
      true
  };
  inline constexpr Fixture traversalHeader{
      "traversalHeader",
      "N3q8ryccAAS6aeE2kAAAAAAAAAAoAAAAAAAAAPIdsAVXpsBg7xEMvxU3Pl30Pbhr+b+dH47n4NiNUF8BLb+qQLZEXog9UMRvw5QR"
      "jfXGCrw5l2YOplbBq5pnZMaYBn2DYHMGXdbBAjtX2RoQ1SF/Trd+OeDAUmcF0Ve7xbWnpbO1bJp+duKy0p4cXpLEZrVG69BuwKVX"
      "NiELyxIqAumi5k//WQ0v0WtL83E0Z+SNWDcXBiABCXAABwsBAAEkBvEHARJTD24kjLjzl3dyR8NHAeb5oOkMbQAA",
      true
  };
  inline constexpr Fixture duplicateHeader{
      "duplicateHeader",
      "N3q8ryccAASx+NF8sAAAAAAAAAAqAAAAAAAAAAQDplhZ8eHI6kPXZx+OPZ1ogwV04VcjXnM7EPwKESw1lTgk3oozwadI1uZ2NXlD"
      "KPMC1hjo8VLozG6cD8sOYDLxhLXTq0DQuD5VdkbnCk6C+xrzPG92MMHbmTBF5MCpAG+SyamL9t19b61UTqPM6fAMgtMrLbCbPvAc"
      "iT9Ek9K0zJiIDTmmdEKn/+a/7u/Se/KBF3Xq/dE9ZywU//3pssOhs8Q2xCdifrrLCgLqcgDg+/bZVRcGIAEJgJAABwsBAAEkBvEH"
      "ARJTD8SzR81HKk2929OoTChZehkMgIEAAA==",
      true
  };
  inline constexpr Fixture ancestorHeader{
      "ancestorHeader",
      "N3q8ryccAASplPT9wAAAAAAAAAAqAAAAAAAAAMJnp32+sCWgW0TO6pkuwdFKpP2JuNgIas9fRvc4rmhOXF4EY9yuGP6tgc4LMxqy"
      "/R7wKku8fXceB+Hoxqb9fgEhRlB+UBJlfet3LWpcR4sQdkGXaO33uVvwOlXO+JBHlWFAqEvo6ExwTg+wenXw+wR0SWY8LlJoMWeq"
      "iVPKAvpG1/p8YSPZT+IS/InpI52H41a1p1yIgUKG6PiW+I2jCOXOwboxPuRhLI0TS/lfe5J9ggGXZHazOP9Rl9DY0651AFaeMNMX"
      "BiABCYCgAAcLAQABJAbxBwESUw+PPGGKyMaphxHAmOkFb7SMDICVAAA=",
      true
  };
  inline constexpr Fixture compressedHeader{
      "compressedHeader",
      "N3q8ryccAARnLIN18AAAAAAAAAA+AAAAAAAAAA9IC0/TR16A/ExvQxRA//hd3AY31BQXqrC2O28b5vhJtleMdcn4pTcfml1owMLO"
      "0enAf6r0wz23pPQmzof24VJfTMU3jypfXF65ZY2N+SGMjoxg1cWaL+vYGYB4zM6Hn42b/jWICkMxyWcidJdjMvYqkt0o2RBgXxUy"
      "m0f5SJJKVEqvoF6VH8nJv6WhRbsA1wW/tqzlsOGANQJL2QFUPGzEYg0pEYR4SQrFU1PN5zU95HNakXdKsgVgtR7QMmkLOkjWnu8W"
      "b1rtKHq0pBqkP8D0IkKJNPgTWRpDXqKjqmUjqA/0iRxzth8fxdwhhx16PLJo19IXBiABCYDQAAcLAQACJAbxBwESUw8Kp3GYRvuO"
      "w2Do4NRQAqHBIwMBAQVdABAAAAEADIDPilYKAcdEXdoAAA==",
      true
  };
  inline constexpr Fixture linkHeader{
      "linkHeader",
      "N3q8ryccAASQRmBPoAAAAAAAAAA8AAAAAAAAAEEIYNL9wMCn5OyGYNfEdW1d3GAYcK993LoTpYYgmKukcNmRZqDMbh1Fbvh9Q5pw"
      "mLBmqbpt+Vi16oTmIsW5tRUy5QoWhEYqq554MX+Dw/Pj6yq3tP6pthadY9mIZuOzAnveIWwsIvu9ylLw4vknq+pFJXaxQxjkFQjr"
      "4MkBuPFT07MD+jJ/tB4pAIaTJD3STuoyf4GJaGGL4rnrUkPYaY4Z6duFFwYgAQmAgAAHCwEAAiQG8QcBElMPaJSaMQLd3RJGTE/j"
      "RDZVRyMDAQEFXQAQAAABAAx1fgoBFiCGXwAA",
      true
  };
  inline constexpr Fixture clearDataHeader{
      "clearDataHeader",
      "N3q8ryccAARhv0hSiwAAAAAAAAAoAAAAAAAAAOz178EBACZjbGVhciBmaWxlIGRhdGEgd2l0aCBlbmNyeXB0ZWQgbWV0YWRhdGEA"
      "2yIjWe5OX2GwnbChfFC6OjWjg0ON/scXsR6P56MIYIkNM33BfeP/7YM17SOJAtJl/4DpbNjJMXTjUKKppb4YmULwt9JKl5Mq5F0o"
      "5t6rFOa6Th6aBgfOxgJjmUY/P4DaFwYrAQlgAAcLAQABJAbxBwESUw/dXPXoKZonkvYEpTJ/H8gVDFUAAA==",
      true
  };
  inline constexpr Fixture invalidPackHeader{
      "invalidPackHeader",
      "N3q8ryccAATU1fYsiwAAAAAAAAAoAAAAAAAAAGmFlOUBACZjbGVhciBmaWxlIGRhdGEgd2l0aCBlbmNyeXB0ZWQgbWV0YWRhdGEA"
      "n5lm7LNcLEsHrMDIm2vf0P5w4cW3EoqinQoANuEb8gMjoiiMmTHGepd7v/BEAtbKTAdzpWvQ0xDZVTZfYe/tfae6dpMjlsi953s6"
      "9Vzh2IFEndUJrCOxphSg28RkKHBLFwYrAQlgAAcLAQABJAbxBwESUw/j0Oou4Cxuncf5HVKHelpHDFkAAA==",
      true
  };
  // Own 7-Zip 26.04 payload; public password: " fixture-🌙 ".
  inline constexpr Fixture astralHeader{
      "astral-header",
      "N3q8ryccAARG5tIgoAAAAAAAAAA8AAAAAAAAAMJmdIm9l4N4h4MLsQJxN4db3s8RsELcBn759sPv1+5rq/yPtoI8rzzouzjz4A74"
      "4B9WU4vb/pP6C1LxlNWaUFYkiGsVsJmZ4jff5EBZj9EEefkZ1QLMi+ifxHGoLkUYE2Lyqrq8ynKH0EojVR1uG1VXwaHjKflax2aO"
      "4ImSqn/0Mcn6JQsytjjgjAgiJrojr75bWhwKEAiOEjGJxpnVpewtQI2rFwYgAQmAgAAHCwEAAiQG8QcBElMP8xyrbHBnJyONqmwn"
      "L7tdsCMDAQEFXQAQAAABAAxzdgoBnfPyUQAA",
      true
  };
} // namespace sevenzip_fixtures
