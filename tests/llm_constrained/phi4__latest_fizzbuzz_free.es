{
  "program": {
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
                "then": [
                  {
                    "printf": {
                      "format": "%s\\n",
                      "args": ["FizzBuzz"]
                    }
                  }
                ],
                "else": {
                  "if_statement": {
                    "condition": "(i % 3) == 0",
                    "then": [
                      {
                        "printf": {
                          "format": "%s\\n",
                          "args": ["Fizz"]
                        }
                      }
                    ],
                    "else": {
                      "if_statement": {
                        "condition": "(i % 5) == 0",
                        "then": [
                          {
                            "printf": {
                              "format": "%s\\n",
                              "args": ["Buzz"]
                            }
                          }
                        ],
                        "else": [
                          {
                            "printf": {
                              "format": "%d\\n",
                              "args": ["i"]
                            }
                          }
                        ]
                      }
                    }
                  }
                }
              }
            }
          ]
        }
      }
    ]
  }
}