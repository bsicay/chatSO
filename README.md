## Proyecto Final:  Chat 


  

  

### Requerimientos
  

####  `protoc`


Ejecutar los siguientes comandos

```bash

sudo  apt  update && sudo  apt  install  -y  protobuf-compiler  libprotobuf-dev

```



## Compilación

Generar los protobuf necesarios:

  
```bash

protoc --cpp_out= chat.proto

```

  

Compilar cliente y server

 

```bash

g++ -o client client.cpp chat.pb.cc ./messageUtil/message.cpp ./messageUtil/constants.h -lprotobuf

g++ -o server server.cpp chat.pb.cc ./utils/message.cpp ./utils/constants.h -lpthread -lprotobuf
```

  
**Ejecución**

```

./server puerto name

```

y

  

```bash

./client server_ip server_port userName

```
