{
  "main_body": [
    {
      "for": {
        "i": {
          "from": 1,
          "to": 30
        },
        "body": [
          {
            "if": {
              "expr": {
                "%d %mod% 15": {
                  "==": [
                    0
                  ]
                }
              },
              body": [
                {
                  "printf": [
                    "FizzBuzz"
                  ]
                }
              ],
              else_body": []
            }
          },
          {
            "if": {
              "expr": {
                "%d %mod% 3": {
                  "==": [
                    0
                  ]
                }
              },
              body": [
                {
                  "printf": [
                    "Fizz"
                  ]
                }
              ],
              else_body": []
            }
          },
          {
            "if": {
              "expr": {
                "%d %mod% 5": {
                  "==": [
                    0
                  ]
                }
              },
              body": [
                {
                  "printf": [
                    "Buzz"
                  ]
                }
              ],
              else_body": [
                {
                  "printf": [
                    "%d\n",
                    "%d"
                  ]
                }
              ]
            }
          }
        ]
      }
    }
  ]
}