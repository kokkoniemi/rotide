(ns app.handler)

(defn handle [request]
  (let [method (:method request)
        path (:uri request)]
    (case method
      :get {:status 200 :body path}
      :post {:status 201}
      {:status 405})))

(def routes
  {"/health" handle
   "/status" handle})
