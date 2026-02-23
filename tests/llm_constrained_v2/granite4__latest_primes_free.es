{
  "program": [
    {
      "type": "variable",
      "name": "i",
      "value": "1"
    },
    {
      "type": "while",
      "condition": "i <= 50",
      "body": [
        {
          "type": "variable",
          "name": "isPrime",
          "value": "1"
        },
        {
          "type": "for",
          "start": "j := 2",
          "end": "= i",
          "step": "+ 1",
          "body": [
            {
              "type": "if",
              "condition": "i % j == 0",
              "then": [
                {
                  "type": "variable",
                  "name": "isPrime",
                  "value": "0"
                },
                {
                  "type": "break"
                }
              ]
            }
          ]
        },
        {
          "type": "if",
          "condition": "isPrime",
          "then": [
            {
              "type": "print",
              "expr": "i"
            }
          ]
        }
      ]
    }
  ]
}