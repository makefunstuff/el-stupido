-stupido
{
  "main": {
    "variables": [
      {"name": "n", "type": "int"},
      {"name": "i", "type": "int"},
      {"name": "is_prime", "type": "bool"}
    ],
    "body": [
      {"for": {
        "init": {"x": 2, "val": 2},
        "condition": {"expr": "n <= 50"},
        "increment": {"x": "n = n + 1"},
        "main_body": [
          {"assign": {"x": "is_prime", "val": true}},
          {"for": {
            "init": {"i": 2, "val": 2},
            "condition": {"expr": "i * i <= n"},
            "increment": {"i": "i = i + 1"},
            "main_body": [
              {"if": {
                "cond": {"expr": "n % i == 0"},
                "then": {"assign": {"x": "is_prime", "val": false}},
                "else": null
              }}
            ]
          }},
          {"if": {
            "cond": {"expr": "is_prime"},
            "then": {"call": {"func": "print", "args": ["n"]}},
            "else": null
          }}
        ]
      }}
    ]
  }
}