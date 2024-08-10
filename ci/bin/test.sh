

temp_role='{ "Credentials": { "AccessKeyId": "ASIAQ3EGRX7CBHS4PLX7", "SecretAccessKey": "DJT8UMWqPKCsToROwM9vrAzAx8tz+OK7JThZDNN/", "SessionToken": "IQoJb3JpZ2luX2VjEOT//////////wEaDmFwLXNvdXRoZWFzdC0yIkcwRQIgRzpnMT3wGz01AaoHGvRIO5qrHtQXdd+9T01b9+t9klQCIQDUg2F0smxJCdTJIsTJT03Fy9o64GpcmRcNZfKoFSd4niqWAggtEAAaDDA1ODI2NDI0MDA2OCIMSIhe9Gg90m+4PO6eKvMB7ZvZbMCWDN/z4173iMlGO6o5tk5t1YxIxHBpC36yr61LY9l1QnnBIKfOLPp6GaL1/jeVCbfv9QYTlZMMQVhr/0oh7a7aPD4OpC5BlsL2uW7WsdMmeEbrHjl/iiT7d7jf42BmsU4jV9bJueO6THbH4owPPvLer/WmMyw5q/DkE3C2mkuWL5om1641wBnA8rxgFeCutYb7XP5AMtyB+motNW+tzJsYnlmUO+vLgawB1JkTje7XX+FyhqSUEqzaWAFoYlmC2zFZ33oqHK83Cu7awq8hhyV8wNEWGxs7P+RVvkhs/dBdLatdQb9RXww6Nnhtt7QOMJLcjrEGOp0BZ5LTGx/SbqbAVETxHsMycJBAUeMDPoxEieIW9cH1KxX6Un8FyDmRWcP5rLU8ZJtaZoHjQyJDup0oT6eKY3ZuvYehNyItfYwYk9IRJh3TOgF0+NeWLJ4FFUfi2xYwlo99WGq7evP/H2dtVhxWpxGQsSLEZ7zy9bSWXDI1yirTyxZGzdHz1ZiokTnKEzAmA81GncwChHGZrw51DCx3sg==", "Expiration": "2024-04-20T12:59:14+00:00" }, "AssumedRoleUser": { "AssumedRoleId": "AROAQ3EGRX7CJFGSFF36R:test-role", "Arn": "arn:aws:sts::058264240068:assumed-role/cdk-hnb659fds-deploy-role-058264240068-ap-southeast-2/test-role" } }'


echo "$temp_role" | jq -r .Credentials.AccessKeyId
