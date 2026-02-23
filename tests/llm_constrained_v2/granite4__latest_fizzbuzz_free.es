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
                  "value": 0
                }
              },
              body": [
                {
                  "printf": {
                    "fmt": "FizzBuzz",
                    "args": []
                  }
                }
              ]
            }
          },
          {
            "if": {
              "expr": {
                "%d %mod% 3": {
                  "value": 0
                }
              },
              body": [
                {
                  "printf": {
                    "fmt": "Fizz",
                    "args": []
                  }
                }
              ]
            }
          },
          {
            "if": {
              "expr": {
                "%d %mod% 5": {
                  "value": 0
                }
              },
              body": [
                {
                  "printf": {
                    "fmt": "Buzz",
                    "args": []
                  }
                }
              ]
            }
          },
          {
            "if": {
              "expr": {
                "%d %mod% 15": {
                  "value": 0
                },
                "%d %mod% 3": {
                  "value": 0
                },
                "%d %mod% 5": {
                  "value": 0
                }
              },
              body": [
                {
                  "printf": {
                    "fmt": "%d",
                    "args": [
                      {
                        "i": {}
                      }
                    ]
                  }
                }
              ]
            }
          }
        ]
      }
    }
  ]
}