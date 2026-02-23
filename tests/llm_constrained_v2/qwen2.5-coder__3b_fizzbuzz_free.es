{
  "main_body": [
    {
      "for_loop": {
        "start": 1,
        "end": 30,
        "body": [
          {
            "if_statement": {
              "condition": {
                "modulus": {
                  "value": 15,
                  "operator": "%",
                  "operand": "$i"
                }
              },
              "then": [
                {
                  "printf": {
                    "string": "FizzBuzz\n"
                  }
                }
              ]
            },
            "else_if_statement": [
              {
                "condition": {
                  "modulus": {
                    "value": 3,
                    "operator": "%",
                    "operand": "$i"
                  }
                },
                "then": [
                  {
                    "printf": {
                      "string": "Fizz\n"
                    }
                  }
                ]
              },
              {
                "condition": {
                  "modulus": {
                    "value": 5,
                    "operator": "%",
                    "operand": "$i"
                  }
                },
                "then": [
                  {
                    "printf": {
                      "string": "Buzz\n"
                    }
                  }
                ]
              }
            ],
            "else": [
              {
                "printf": {
                  "value": "$i"
                }
              }
            ]
          }
        ]
      }
    }
  ]
}