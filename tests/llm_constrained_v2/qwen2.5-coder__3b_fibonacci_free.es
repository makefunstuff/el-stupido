{
  "functions": [
    {
      "name": "fib",
      "params": ["n"],
      "body": "n < 2 ? n : fib(n-1) + fib(n-2)"
    }
  ],
  "main": [
    {
      "type": "for",
      "var": "i",
      "start": 1,
      "end": 20,
      "action": {
        "type": "print",
        "expr": {
          "func": "fib",
          "args": ["i"]
        }
      }
    }
  ]
}