{
  "main_body": [
    {
      "for_loop": {
        "variable": "i",
        "start": 1,
        "end": 30,
        "body": [
          {
            "if_statement": {
              "condition": "(i % 15) == 0",
              "then": {
                "printf": ["%s\\n", "\"FizzBuzz\""]
              },
              "else_if": {
                "condition": "(i % 3) == 0",
                "then": {
                  "printf": ["%s\\n", "\"Fizz\""]
                }
              },
              "else_if": {
                "condition": "(i % 5) == 0",
                "then": {
                  "printf": ["%s\\n", "\"Buzz\""]
                }
              },
              "else": {
                "printf": ["%d\\n", "i"]
              }
            }
          }
        ]
      }
    }
  ]
}