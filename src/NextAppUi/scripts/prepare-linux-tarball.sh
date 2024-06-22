#!/bin/bash

APP_NAME="nextapp"
APP_DIR="${APP_NAME}_deployment"
APP_PATH="bin/${APP_NAME}"

# Create the deployment directory
mkdir -p $APP_DIR

# Copy the application executable
cp -v ${APP_PATH} *.so  $APP_DIR

# Identify and copy required Qt libraries
LIBS=$(ldd bin/${APP_NAME} | grep '/opt/Qt/' | awk '{print $3}')
for LIB in $LIBS; do
    cp $LIB $APP_DIR
done

# Create a run script
cat <<EOF > $APP_DIR/run.sh
#!/bin/bash
export LD_LIBRARY_PATH=\$(dirname "\$0"):\$LD_LIBRARY_PATH
./$APP_NAME
EOF
chmod +x $APP_DIR/run.sh

# Create a tarball
tar -cvzf ${APP_NAME}.tar.gz $APP_DIR

# Cleanup
rm -rf $APP_DIR

echo "Deployment package created: ${APP_NAME}.tar.gz"

# Function to find the package that provides a given library
find_package() {
    LIB=$1
    PKG=$(dpkg -S $LIB 2>/dev/null | cut -d ':' -f 1)
    if [ -z "$PKG" ]; then
        PKG=$(dpkg-query -S $LIB 2>/dev/null | awk -F: '{print $1}')
    fi
    echo $PKG
}

# Get the list of shared libraries, excluding those with /opt/ in their path
LIBS=$(ldd $APP_PATH | awk '{print $3}' | grep -v '^$' | egrep -v '/opt/|/var/local/build')

# Initialize an array to store package names
declare -A PACKAGES

echo "Identifying required .deb packages for the application..."

# Iterate over each library
for LIB in $LIBS; do
    if [ -e "$LIB" ]; then
        PKG=$(find_package $LIB)
        if [ ! -z "$PKG" ]; then
            PACKAGES[$PKG]=1
        else
            echo "Warning: No package found for library $LIB"
        fi
    else
        echo "Warning: Library $LIB not found"
    fi
done

# Print the list of packages
echo "Required .deb packages:"
for PKG in "${!PACKAGES[@]}"; do
    echo $PKG
done
