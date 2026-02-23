{
  "main_body": {
    "statements": [
      {
        "type": "for",
        "variables": ["i"],
        "range": {"start": 2, "end": 50},
        "body": {
          "statements": [
            {
              "type": "if",
              "condition": {
                "expression": {
                  "type": "modulo",
                  "left": {
                    "type": "variable",
                    "name": "i"
                  },
                  "right": {
                    "type": "variable",
                    "name": "2"
                  }
                }
              },
              "body": [
                {
                  "type": "break"
                }
              ]
            },
            {
              "type": "print",
              "expression": {
                "type": "variable",
                "name": "i"
              }
            }
          ]
        }
      }
    ]
  }
}