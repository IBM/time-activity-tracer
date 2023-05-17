# Reader data download

Flow chart for interacting with tags from the reader:

~~~mermaid
graph TD

A[Start]-->B{Is Auto-download enabled?}

B-- Yes -->C[Wait for tag]
B-- No -->D[Send '3' to start download]

C-->E[Put tag on reader]
D-->E

E-->F{Was a tag detected?}

F-- Yes -->G[Wait for data]
F-- No -->B

G-->H{Starts with pipe?}

H-- Yes -->I[Upload data]
H-- No -->J{Starts with 'Done'?}

I-->G

J-- Yes -->K[End]
J-- No -->G
~~~
