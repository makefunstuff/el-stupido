{
  "program": {
    "variables": [
      {"name": "n", "type": "int"},
      {"name": "i", "type": "int"},
      {"name": "is_prime", "type": "bool"}
    ],
    "main_body": [
      {"for": [
        {"init": "n := 2"},
        {"condition": "n <= 50"},
        {"increment": "n = n + 1"},
        {
          "body": [
            {"assign": "is_prime := true"},
            {"for": [
              {"init": "i := 2"},
              {"condition": "i * i <= n"},
              {"increment": "i = i + 1"},
              {
                "body": [
                  {"if": [
                    {"condition": "n % i == 0"},
                    {"then": [
                      {"assign": "is_prime := false"},
                      {"break"}
                    ]}
                  ]}
                ]
              }
            ]},
            {"if": [
              {"condition": "is_prime"},
              {"then": ["print(n)"]}
            ]}
          ]
        }
      ]}
    ]
  }
}