-stupido
{
    "main": {
        "variables": [
            {"name": "n", "type": "int"},
            {"name": "i", "type": "int"},
            {"name": "is_prime", "type": "bool"}
        ],
        "body": [
            {"for": {"start": 2, "end": 50, "step": 1}},
            {
                "assign": {"variable": "n", "value": "i"}
            },
            {
                "assign": {"variable": "is_prime", "value": 1}
            },
            {
                "for": {"start": 2, "end": "n / 2 + 1", "step": 1},
                "body": [
                    {
                        "if": {
                            "condition": "n % i == 0",
                            "then": [
                                {
                                    "assign": {"variable": "is_prime", "value": 0}
                                },
                                {
                                    "break"
                                }
                            ]
                        }
                    }
                ]
            },
            {
                "if": {
                    "condition": "is_prime == 1",
                    "then": [
                        {
                            "print": {"expression": "n"}
                        }
                    ]
                }
            }
        ]
    }
}