{
  "program": {
    "functions": [
      {
        "name": "fib",
        "parameters": ["n"],
        "body": "n < 2 ? n : fib(n-1) + fib(n-2)"
      }
    ],
    "main": {
      "loop": {
        "type": "for",
        "initializer": "i := 1",
        "condition": "i <= 20",
        "increment": "i++",
        "body": [
          {
            "functionCall": {
              "name": "print",
              "arguments": ["fib(i)"]
            }
          }
        ]
      }
    }
  }
}