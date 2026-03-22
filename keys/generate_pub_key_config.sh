openssl ec -pubin -in public.pem -outform DER | tail -c 64 | xxd -i
