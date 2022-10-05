sudo chown user gw
sudo rm -r gw
sudo git clone https://github.com/sonvoque/sls_gw
sudo mv sls_gw gw
sudo chown user gw
sudo chown user gw/*
cd gw
sudo chmod +x automain.sh
sudo gcc -o main main.c $(mysql_config --libs --cflags)
